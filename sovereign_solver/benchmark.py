"""Streaming benchmark, validation, and reporting pipeline for Sovereign.

The runner deliberately invokes the existing C++ CLI once per model, so input
files and solver algorithms stay outside this module. It never bulk-loads a
dataset and never treats an unverified incumbent as a successful solve.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import json
import math
import os
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .classification import classify_model
from .parser import parse_problem_file

SUPPORTED_SUFFIXES = {".mps", ".json", ".txt", ".gz"}
CSV_FIELDS = [
    "dataset", "family", "instance", "problem_class", "number_of_variables",
    "number_of_constraints", "number_of_nonzeros", "solver_method", "backend",
    "presolve_enabled", "status", "objective_value", "best_primal_bound",
    "best_dual_bound", "absolute_gap", "relative_gap", "iterations", "lp_solves",
    "nodes_created", "nodes_processed", "nodes_pruned", "parse_time_ms", "model_preparation_time_ms",
    "presolve_time_ms", "solve_time_ms", "postsolve_time_ms",
    "verification_time_ms", "total_time_ms", "primal_residual", "dual_residual",
    "complementarity_residual", "integer_feasible", "verification_pass",
    "convexity", "hessian_type", "gpu_available", "gpu_device", "gpu_used",
    "gpu_reason", "gpu_kernel_time_ms", "cpu_to_gpu_time_ms", "gpu_to_cpu_time_ms",
    "total_gpu_time_ms", "memory_usage_mb", "failure_reason",
    "minimum_hessian_eigenvalue_or_classification", "reference_solver_status",
    "presolve_applied_to_solve", "presolve_fallback",
]


def _float(text: str | None) -> float | None:
    if text is None:
        return None
    try:
        value = float(text.strip())
        return value if math.isfinite(value) else None
    except (TypeError, ValueError):
        return None


def _field(output: str, label: str) -> str | None:
    match = re.search(rf"(?im)^\s*{re.escape(label)}\s*:\s*(.*?)\s*$", output)
    return match.group(1) if match else None


def _vector(output: str) -> list[float] | None:
    value = _field(output, "Primal")
    if value is None:
        return None
    try:
        return [float(part) for part in value.split()]
    except ValueError:
        return None


def _names(output: str) -> list[str] | None:
    value = _field(output, "Primal Names")
    return value.split() if value is not None else None


def _model_size(model) -> tuple[int, int, int]:
    return len(model.variables), len(model.constraints), sum(len(c.coefficients) for c in model.constraints)


def _total_memory_bytes() -> int | None:
    """Return host RAM from standard library APIs when available."""
    if os.name == "nt":
        try:
            import ctypes
            class MemoryStatus(ctypes.Structure):
                _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                            ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                            ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                            ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                            ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
            status = MemoryStatus()
            status.dwLength = ctypes.sizeof(status)
            return int(status.ullTotalPhys) if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)) else None
        except (AttributeError, OSError):
            return None
    try:
        pages = os.sysconf("SC_PHYS_PAGES")
        page_size = os.sysconf("SC_PAGE_SIZE")
        return int(pages * page_size)
    except (AttributeError, OSError, ValueError):
        return None


def verify_original(model, primal: list[float] | None, tolerance: float = 1e-7,
                    primal_names: list[str] | None = None) -> dict[str, Any]:
    """Check a candidate against original rows, bounds, integrality and objective."""
    if primal_names is not None:
        if len(primal_names) != len(primal or ()) or len(set(primal_names)) != len(primal_names):
            return {"pass": False, "primal_residual": None, "integer_feasible": None, "objective": None,
                    "failure_reason": "solver primal variable-name map is malformed"}
        by_name = dict(zip(primal_names, primal or ()))
        if set(by_name) != {v.name for v in model.variables}:
            return {"pass": False, "primal_residual": None, "integer_feasible": None, "objective": None,
                    "failure_reason": "solver primal variable-name map does not match original model variables"}
        primal = [by_name[v.name] for v in model.variables]
    if primal is None or len(primal) != len(model.variables) or not all(math.isfinite(v) for v in primal):
        return {"pass": False, "primal_residual": None, "integer_feasible": None, "objective": None,
                "failure_reason": "solver output did not contain a finite original-space primal vector"}
    residual = 0.0
    integer_feasible = True
    for i, variable in enumerate(model.variables):
        lower, upper = model.bounds.get(variable.name, (0.0, 1.0 if variable.type == "binary" else None))
        value = primal[i]
        if lower is not None:
            residual = max(residual, float(lower) - value)
        if upper is not None:
            residual = max(residual, value - float(upper))
        if variable.type in {"integer", "binary"}:
            integer_feasible &= abs(value - round(value)) <= tolerance
        if variable.type == "binary":
            residual = max(residual, -value, value - 1.0)
    indexes = {v.name: i for i, v in enumerate(model.variables)}
    for row in model.constraints:
        activity = sum(coefficient * primal[indexes[name]]
                       for name, coefficient in row.coefficients.items())
        violation = (abs(activity - row.rhs) if row.operator == "=" else
                     max(0.0, activity - row.rhs) if row.operator == "<=" else
                     max(0.0, row.rhs - activity))
        residual = max(residual, violation)
    residual = max(0.0, residual)
    objective = model.objective_constant + sum(model.objective.get(v.name, 0.0) * primal[i] for i, v in enumerate(model.variables))
    # Sovereign's diagonal QP convention is 1/2 x'Qx + c'x.
    objective += 0.5 * sum(model.quadratic_terms.get(v.name, 0.0) * primal[i] ** 2
                           for i, v in enumerate(model.variables))
    passed = residual <= tolerance and (integer_feasible or not model.has_discrete_variables)
    return {"pass": passed, "primal_residual": residual, "integer_feasible": integer_feasible,
            "objective": objective,
            "failure_reason": None if passed else "original model feasibility or integrality check failed"}


def _method_for(problem_class: str, method: str) -> tuple[list[str], str]:
    if method == "auto":
        method = {"LP": "revised-simplex", "MILP": "milp", "QP": "qp"}.get(problem_class, "auto")
    if problem_class == "MILP" and method in {"revised-simplex", "dual-simplex", "ipm"}:
        return ["--method", "milp", "--lp-method", method], "branch-and-bound/" + method
    return ["--method", method], method


def _objective_comparison(our_objective: float | None, reference_objective: float | None,
                          tolerance: float) -> tuple[float | None, bool | None]:
    if our_objective is None or reference_objective is None:
        return None, None
    difference = abs(our_objective - reference_objective)
    return difference, difference <= tolerance


def _runtime_comparison(our_solve_ms: float | None, our_total_ms: float | None,
                        reference_solve_ms: float | None) -> dict[str, float | None]:
    """Compare solver measurements while retaining CLI wall time separately."""
    ratio = (our_solve_ms / reference_solve_ms
             if our_solve_ms is not None and reference_solve_ms else None)
    speedup = (reference_solve_ms / our_solve_ms
               if our_solve_ms and reference_solve_ms is not None else None)
    return {"our_solve_time_ms": our_solve_ms, "our_total_time_ms": our_total_ms,
            "our_runtime_ms": our_solve_ms, "runtime_ratio": ratio,
            "speedup_vs_highs": speedup}


def _verification_failure_reason(result: dict[str, Any], check: dict[str, Any],
                                 candidate_status: bool, objective_consistent: bool) -> str | None:
    if result.get("failure_reason"):
        return result["failure_reason"]
    if check.get("failure_reason"):
        return check["failure_reason"]
    if candidate_status and result.get("reported_verification") is False:
        return "solver reported Verification: FAIL"
    if candidate_status and not objective_consistent:
        return "reported objective does not match original-model objective"
    return None


def _parse_solver_output(output: str, returncode: int) -> dict[str, Any]:
    raw_status = (_field(output, "Status") or _field(output, "SOLVER Status") or "FAILED").upper().replace(" ", "_")
    if raw_status == "OPTIMAL_INTEGER":
        status = "OPTIMAL"
    elif raw_status in {"OPTIMAL", "FEASIBLE"}:
        status = raw_status
    elif raw_status == "TIME_LIMIT":
        status = "TIME_LIMIT"
    elif raw_status in {"INFEASIBLE", "UNBOUNDED"}:
        status = raw_status
    elif raw_status.startswith("UNSUPPORTED"):
        status = "UNSUPPORTED"
    elif raw_status in {"ITERATION_LIMIT", "ITERATION_LIMIT_REACHED", "CUT_LIMIT_REACHED", "NO_FEASIBLE_SOLUTION"}:
        status = "FEASIBLE" if _vector(output) is not None else "FAILED"
    elif returncode != 0 or raw_status in {"FAILED", "NUMERICAL_FAILURE", "LP_FAILURE", "LP_SOLVE_FAILED"}:
        status = "NUMERICAL_FAILURE" if "NUMERICAL" in raw_status else "FAILED"
    else:
        status = "FAILED"
    return {
        "raw_status": raw_status,
        "status": status,
        "objective_value": _float(_field(output, "Objective")),
        "best_primal_bound": _first_number(_field(output, "Primal Bound"), _field(output, "Objective")),
        "best_dual_bound": _first_number(_field(output, "Dual Bound"), _field(output, "LP Bound")),
        "absolute_gap": _float(_field(output, "Absolute Gap")),
        "relative_gap": _float(_field(output, "Relative Gap")),
        "iterations": _int(_field(output, "Iterations")),
        "lp_solves": _int(_field(output, "LP Solves")),
        "nodes_created": _int(_field(output, "Nodes Created")),
        "nodes_processed": _int(_field(output, "Nodes Processed")),
        "nodes_pruned": _int(_field(output, "Nodes Pruned")),
        "reported_verification": (_field(output, "Verification") or "").upper() == "PASS",
        "presolve_fallback": (_field(output, "Presolve fallback") or "").upper() == "YES",
        "primal": _vector(output),
        "primal_names": _names(output),
        "primal_residual_reported": _first_number(_field(output, "Primal residual"), _field(output, "Feasibility")),
        "dual_residual": _float(_field(output, "Dual residual")),
        "complementarity_residual": _float(_field(output, "Complementarity")),
        "convexity": _field(output, "Convexity"),
        "hessian_type": _field(output, "Hessian"),
        "failure_reason": None if status in {"OPTIMAL", "FEASIBLE", "INFEASIBLE", "UNBOUNDED"} else (output.strip()[-1200:] or f"solver exited {returncode}"),
    }


def _int(value: str | None) -> int | None:
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None


def _nonnegative_int(value: Any) -> int | None:
    try:
        parsed = int(value)
        return parsed if parsed >= 0 else None
    except (TypeError, ValueError, OverflowError):
        return None


def _first_number(*values: str | None) -> float | None:
    for value in values:
        parsed = _float(value)
        if parsed is not None:
            return parsed
    return None


def _invoke(solver: Path, instance: Path, method_args: list[str], backend: str,
            device: int, timeout: float, no_presolve: bool = False) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="sovereign_benchmark_") as temp_dir:
        solver_instance = instance
        if instance.suffix.lower() == ".gz":
            staged = Path(temp_dir) / instance.name[:-3]
            try:
                with gzip.open(instance, "rb") as compressed, staged.open("wb") as expanded:
                    while chunk := compressed.read(1024 * 1024):
                        expanded.write(chunk)
            except (OSError, EOFError) as exc:
                return {"status": "UNSUPPORTED", "raw_status": "UNSUPPORTED", "solve_time_ms": None,
                        "timed_out": False, "failure_reason": f"cannot decompress gzip benchmark input: {exc}", "primal": None}
            solver_instance = staged
        command = [str(solver), "--input", str(solver_instance), *method_args, "--backend", backend, "--device", str(device)]
        if no_presolve:
            command.append("--no-presolve")
        start = time.perf_counter_ns()
        try:
            proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
            elapsed = (time.perf_counter_ns() - start) / 1e6
            output = proc.stdout + "\n" + proc.stderr
            result = _parse_solver_output(output, proc.returncode)
            parse_ms = _first_number(_field(output, "Parse time ms"))
            presolve_ms = _first_number(_field(output, "Presolve time ms"))
            reported_solve_ms = _first_number(_field(output, "Solve time ms"))
            result.update({"solve_time_ms": reported_solve_ms,
                           "postsolve_time_ms": _first_number(_field(output, "Postsolve time ms")),
                           "cli_verification_time_ms": _first_number(_field(output, "Verification time ms")), "cli_time_ms": elapsed,
                           "build_compiler": _field(output, "Build compiler"),
                           "build_type": _field(output, "Build type"),
                           "cpp_standard": _field(output, "C++ standard"),
                           "parse_time_ms": parse_ms, "presolve_time_ms": presolve_ms,
                           "selected_backend": _field(output, "Backend"), "stdout": proc.stdout, "stderr": proc.stderr,
                           "returncode": proc.returncode, "timed_out": False})
            return result
        except subprocess.TimeoutExpired as exc:
            elapsed = (time.perf_counter_ns() - start) / 1e6
            return {"status": "TIME_LIMIT", "raw_status": "TIME_LIMIT", "solve_time_ms": None,
                    "total_time_ms": elapsed,
                    "stdout": (exc.stdout or b"").decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or ""),
                    "stderr": (exc.stderr or b"").decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or ""),
                    "returncode": None, "timed_out": True, "failure_reason": f"per-instance time limit of {timeout:g} seconds exceeded",
                    "primal": None}


def _instance_paths(root: Path) -> list[Path]:
    return sorted((p for p in root.rglob("*") if p.is_file() and p.name != ".gitkeep"), key=lambda p: p.as_posix().lower())


def _gpu_info(solver: Path, device: int) -> dict[str, Any]:
    try:
        p = subprocess.run([str(solver), "--device-info", "--device", str(device)], capture_output=True,
                           text=True, timeout=15, check=False)
        text = p.stdout + p.stderr
        return {"available": "CUDA Available: YES" in text,
                "device": _field(text, "Device"), "runtime": _field(text, "CUDA Runtime"), "raw": text.strip()}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"available": False, "device": None, "runtime": None, "raw": str(exc)}


def _reference_highs(model, time_limit_seconds: float | None = None) -> dict[str, Any]:
    if model.has_quadratic_objective:
        return {"reference_solver_status": "UNSUPPORTED", "failure_reason": "HiGHS comparison currently covers LP/MILP only"}
    try:
        import highspy
    except ImportError:
        return {"reference_solver_status": "NOT_AVAILABLE", "failure_reason": "optional highspy package is not installed"}
    try:
        highs = highspy.Highs()
        highs.setOptionValue("output_flag", False)
        if time_limit_seconds is not None:
            highs.setOptionValue("time_limit", float(time_limit_seconds))
        inf = highspy.kHighsInf
        indices_by_name = {v.name: i for i, v in enumerate(model.variables)}
        for i, variable in enumerate(model.variables):
            lo, hi = model.bounds.get(variable.name, (0.0, 1.0 if variable.type == "binary" else None))
            highs.addVar(-inf if lo is None else float(lo), inf if hi is None else float(hi))
            highs.changeColCost(i, float(model.objective.get(variable.name, 0.0)))
            if variable.type in {"integer", "binary"}:
                highs.changeColIntegrality(i, highspy.HighsVarType.kInteger)
        for row in model.constraints:
            lo, hi = -inf, inf
            if row.operator == "=": lo = hi = float(row.rhs)
            elif row.operator == "<=": hi = float(row.rhs)
            else: lo = float(row.rhs)
            idx = [indices_by_name[name] for name in row.coefficients]
            val = [float(value) for value in row.coefficients.values()]
            highs.addRow(lo, hi, len(idx), idx, val)
        highs.setMaximize() if model.objective_sense == "maximize" else highs.setMinimize()
        start = time.perf_counter_ns()
        highs.run()
        elapsed = (time.perf_counter_ns() - start) / 1e6
        model_status = str(highs.modelStatusToString(highs.getModelStatus())).upper()
        info, solution = highs.getInfo(), highs.getSolution()
        status = "OPTIMAL" if "OPTIMAL" in model_status else "FEASIBLE" if solution.value_valid else "FAILED"
        return {"reference_solver_status": status, "reference_status_text": model_status,
                "reference_objective": _float(str(info.objective_function_value)) if solution.value_valid else None,
                "reference_runtime_ms": elapsed,
                "reference_iterations": getattr(info, "simplex_iteration_count", None),
                "reference_nodes": _nonnegative_int(getattr(info, "mip_node_count", None)),
                "reference_gap": _float(str(getattr(info, "mip_gap", ""))), "failure_reason": None}
    except Exception as exc:  # external APIs vary across installed highspy versions
        return {"reference_solver_status": "FAILED", "failure_reason": f"HiGHS adapter error: {exc}"}


def _empty_row(dataset: str, instance: Path) -> dict[str, Any]:
    row = {key: None for key in CSV_FIELDS}
    row.update({"dataset": dataset, "family": dataset, "instance": str(instance), "presolve_enabled": True,
                "backend": "cpu", "status": "UNSUPPORTED", "gpu_available": False,
                "gpu_used": False, "gpu_reason": "not evaluated"})
    return row


def _average(values: list[float | int]) -> str:
    return f"{statistics.mean(values):.6g}" if values else "n/a"


def _aggregate(rows: list[dict[str, Any]], references: list[dict[str, Any]], gpu_rows: list[dict[str, Any]]) -> dict[str, Any]:
    lp = [r for r in rows if r["problem_class"] == "LP"]
    milp = [r for r in rows if r["problem_class"] == "MILP"]
    qp = [r for r in rows if r["problem_class"] == "QP"]
    speedups = [r["speedup"] for r in gpu_rows if isinstance(r.get("speedup"), (int, float))]
    compared = [r for r in references if r.get("reference_solver_status") in {"OPTIMAL", "FEASIBLE"}]
    times = lambda rs: [r["total_time_ms"] for r in rs if isinstance(r.get("total_time_ms"), (int, float))]
    return {
        "total_instances": len(rows),
        "successful_instances": sum(r["status"] in {"OPTIMAL", "FEASIBLE"} and r["verification_pass"] is True for r in rows),
        "failed_instances": sum(r["status"] in {"FAILED", "NUMERICAL_FAILURE"} for r in rows),
        "timeouts": sum(r["status"] == "TIME_LIMIT" for r in rows),
        "unsupported_instances": sum(r["status"] == "UNSUPPORTED" for r in rows),
        "lp_optimal_count": sum(r["status"] == "OPTIMAL" for r in lp),
        "lp_average_runtime_ms": statistics.mean(times(lp)) if times(lp) else None,
        "lp_median_runtime_ms": statistics.median(times(lp)) if times(lp) else None,
        "milp_optimal_count": sum(r["status"] == "OPTIMAL" for r in milp),
        "milp_feasible_count": sum(r["status"] in {"OPTIMAL", "FEASIBLE"} and r["verification_pass"] is True for r in milp),
        "milp_average_nodes": statistics.mean([r["nodes_created"] for r in milp if r.get("nodes_created") is not None]) if any(r.get("nodes_created") is not None for r in milp) else None,
        "milp_average_relative_gap": statistics.mean([r["relative_gap"] for r in milp if r.get("relative_gap") is not None]) if any(r.get("relative_gap") is not None for r in milp) else None,
        "milp_median_runtime_ms": statistics.median(times(milp)) if times(milp) else None,
        "qp_convex_count": sum(str(r.get("convexity", "")).upper() == "CONVEX" for r in qp),
        "qp_optimal_count": sum(r["status"] == "OPTIMAL" for r in qp),
        "qp_numerical_failure_count": sum(r["status"] == "NUMERICAL_FAILURE" for r in qp),
        "qp_average_runtime_ms": statistics.mean(times(qp)) if times(qp) else None,
        "cpu_runs": sum(r.get("cpu_time_ms") is not None for r in gpu_rows),
        "gpu_runs": sum(r.get("gpu_time_ms") is not None for r in gpu_rows),
        "gpu_successes": sum(r.get("gpu_used") is True for r in gpu_rows),
        "average_gpu_speedup": statistics.mean(speedups) if speedups else None,
        "external_instances_compared": len(compared),
        "external_objective_matches": sum(r.get("objective_match") is True for r in compared),
        "our_faster_count": sum((r.get("speedup_vs_highs") or 0) > 1 for r in compared),
        "reference_faster_count": sum((r.get("speedup_vs_highs") or 0) < 1 for r in compared),
        "near_equal_runtime_count": sum(r.get("speedup_vs_highs") is not None and math.isclose(r["speedup_vs_highs"], 1.0, rel_tol=0.05) for r in compared),
    }


def _summary(dataset: str, rows: list[dict[str, Any]], metadata: dict[str, Any]) -> str:
    classes = ("LP", "MILP", "QP")
    lines = [f"# Benchmark summary: {dataset}", "", f"Generated: {metadata['timestamp_utc']}", "",
             "## Executive summary and run overview", "", f"- Instances: {len(rows)}",
             f"- Successful verified solves: {sum(r['status'] in {'OPTIMAL', 'FEASIBLE'} and r['verification_pass'] is True for r in rows)}",
             f"- Timeouts: {sum(r['status'] == 'TIME_LIMIT' for r in rows)}",
             f"- Unsupported: {sum(r['status'] == 'UNSUPPORTED' for r in rows)}",
             f"- Other failures: {sum(r['status'] in {'FAILED', 'NUMERICAL_FAILURE'} for r in rows)}", "",
             "## Results by class", "", "| Class | Instances | Optimal | Feasible | Failures | Median total time (ms) |", "|---|---:|---:|---:|---:|---:|"]
    for problem_class in classes:
        subset = [r for r in rows if r["problem_class"] == problem_class]
        times = [r["total_time_ms"] for r in subset if isinstance(r["total_time_ms"], (int, float))]
        lines.append(f"| {problem_class} | {len(subset)} | {sum(r['status']=='OPTIMAL' for r in subset)} | {sum(r['status']=='FEASIBLE' for r in subset)} | {sum(r['status'] in {'FAILED','NUMERICAL_FAILURE','TIME_LIMIT'} for r in subset)} | {statistics.median(times) if times else 'n/a'} |")
    aggregates = metadata.get("aggregates", {})
    lines += ["", "## Aggregate statistics", "",
              f"- Total: {len(rows)}; verified successes: {sum(r['status'] in {'OPTIMAL', 'FEASIBLE'} and r['verification_pass'] is True for r in rows)}; timeouts: {sum(r['status'] == 'TIME_LIMIT' for r in rows)}; unsupported: {sum(r['status'] == 'UNSUPPORTED' for r in rows)}",
              f"- MILP average nodes: {_average([r['nodes_created'] for r in rows if r['problem_class'] == 'MILP' and r['nodes_created'] is not None])}; average reported gap: {_average([r['relative_gap'] for r in rows if r['problem_class'] == 'MILP' and r['relative_gap'] is not None])}", "",
              "## CPU/GPU measurements", "",
              f"- CPU/GPU pairs: {aggregates.get('cpu_runs', 0)} CPU runs, {aggregates.get('gpu_runs', 0)} CUDA runs, {aggregates.get('gpu_successes', 0)} verified CUDA results; mean measured speedup: {aggregates.get('average_gpu_speedup') if aggregates.get('average_gpu_speedup') is not None else 'n/a'}.",
              "- GPU timings are CLI wall times. Kernel and transfer splits remain unavailable because the CUDA context does not expose event counters. No speedup is claimed from unsupported or failed pairs.", ""]
    if metadata.get("gpu_info"):
        lines.append(f"CUDA device: {metadata['gpu_info'].get('device') or 'unavailable'} ({metadata['gpu_info'].get('runtime') or 'runtime unavailable'}).")
    lines += ["", "## External solver comparison", "",
              f"- Instances compared: {aggregates.get('external_instances_compared', 0)}; objective matches: {aggregates.get('external_objective_matches', 0)}; our runtime lower: {aggregates.get('our_faster_count', 0)}; reference runtime lower: {aggregates.get('reference_faster_count', 0)}; near-equal runtimes: {aggregates.get('near_equal_runtime_count', 0)}.",
              "- HiGHS results are included only when the optional `highspy` package is present. Missing/unusable references are labeled in `external_solver_comparison.csv`.", "",
              "## Presolve", "", f"- ON/OFF pairs: {metadata.get('presolve_aggregates', {}).get('instances', 0)}; original-model checks passing without presolve: {metadata.get('presolve_aggregates', {}).get('verified_without_presolve', 0)}; with presolve: {metadata.get('presolve_aggregates', {}).get('verified_with_presolve', 0)}.",
              "- Timings are per CLI invocation and include process startup, parsing, and output; do not interpret tiny differences as solver speedups. Presolve is applied to LP solves only; other method paths are marked in their records.", "",
              "## Reproducibility", "", f"- Solver executable: `{metadata.get('solver_path')}`", f"- OS: {metadata['system']} {metadata['release']}; Python {metadata['python_version']}", f"- Time limit: {metadata['time_limit_seconds']} seconds per instance", f"- Tier: {metadata['tier']}", "- Instances are parsed, solved, verified, and released one at a time.", "",
              "## Failures and unsupported inputs", "", "See the per-instance JSON/CSV rows for parser, unsupported-format, timeout, solver, and original-model verification reasons. No external benchmark values are inferred or fabricated.", ""]
    return "\n".join(lines)


def _write_csv(path: Path, rows: list[dict[str, Any]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def run_benchmark(args: argparse.Namespace) -> dict[str, Any]:
    root, solver, out_dir = Path(args.input), Path(args.solver), Path(args.output_dir)
    if not root.exists():
        raise FileNotFoundError(f"benchmark input directory does not exist: {root}")
    if not solver.is_file():
        raise FileNotFoundError(f"solver executable does not exist: {solver}")
    gpu = _gpu_info(solver, args.device)
    paths = _instance_paths(root)
    tier_limit = {"quick": 10, "standard": 50, "full": None}[args.tier]
    limit = args.limit if args.limit is not None else tier_limit
    if limit is not None:
        paths = paths[:limit]
    rows: list[dict[str, Any]] = []
    external_rows: list[dict[str, Any]] = []
    gpu_rows: list[dict[str, Any]] = []
    presolve_rows: list[dict[str, Any]] = []
    build_info: dict[str, str | None] = {}
    for path in paths:
        instance_start = time.perf_counter_ns()
        row = _empty_row(args.dataset, path)
        row.update({"gpu_available": gpu["available"], "gpu_device": gpu["device"], "gpu_reason": "not requested"})
        parse_start = time.perf_counter_ns()
        if path.suffix.lower() not in SUPPORTED_SUFFIXES:
            row.update({"status": "UNSUPPORTED",
                        "failure_reason": (f"input extension {path.suffix or '(none)'} is unsupported; "
                                           "current parsers accept .mps, .json, and .txt files")})
            row["total_time_ms"] = (time.perf_counter_ns() - instance_start) / 1e6
            rows.append(row)
            continue
        try:
            model = parse_problem_file(str(path))
            parsed_ms = (time.perf_counter_ns() - parse_start) / 1e6
            classification = classify_model(model).problem_type
            nvar, nrow, nnz = _model_size(model)
            method_args, effective_method = _method_for(classification, args.method)
            try:
                relative_family = path.parent.relative_to(root).as_posix()
                family = args.dataset if relative_family == "." else relative_family
            except ValueError:
                family = args.dataset
            effective_backend = "cpu" if args.backend == "cuda" and not gpu["available"] else args.backend
            row.update({"problem_class": classification, "number_of_variables": nvar,
                        "number_of_constraints": nrow, "number_of_nonzeros": nnz,
                        "family": family, "solver_method": effective_method, "backend": effective_backend,
                        "presolve_enabled": not args.no_presolve, "parse_time_ms": parsed_ms,
                        "model_preparation_time_ms": parsed_ms,
                        "gpu_used": args.backend == "cuda" and gpu["available"],
                        "presolve_applied_to_solve": classification == "LP"})
            if classification == "QP" and method_args != ["--method", "qp"]:
                row.update({"status": "UNSUPPORTED", "failure_reason": f"method {args.method} does not support QP"})
                row["total_time_ms"] = (time.perf_counter_ns() - instance_start) / 1e6
                rows.append(row)
                continue
            if classification == "LP" and args.method not in {"auto", "revised-simplex", "dual-simplex", "ipm"}:
                row.update({"status": "UNSUPPORTED", "failure_reason": f"method {args.method} is not an LP method"})
                row["total_time_ms"] = (time.perf_counter_ns() - instance_start) / 1e6
                rows.append(row)
                continue
            if classification == "MILP" and args.method not in {"auto", "revised-simplex", "dual-simplex", "ipm", "milp", "lp-relaxation", "cutting-plane", "feasibility-pump"}:
                row.update({"status": "UNSUPPORTED", "failure_reason": f"method {args.method} is not supported for MILP"})
                row["total_time_ms"] = (time.perf_counter_ns() - instance_start) / 1e6
                rows.append(row)
                continue
            result = _invoke(solver, path, method_args, effective_backend, args.device, args.time_limit,
                             no_presolve=args.no_presolve)
            for key in ("build_compiler", "build_type", "cpp_standard"):
                if result.get(key):
                    build_info[key] = result[key]
            verify_start = time.perf_counter_ns()
            check = verify_original(model, result.get("primal"), args.feasibility_tol, result.get("primal_names"))
            verify_ms = (time.perf_counter_ns() - verify_start) / 1e6
            status = result["status"]
            candidate_status = status in {"OPTIMAL", "FEASIBLE"}
            verification_pass = (check["pass"] and result.get("reported_verification", True)) if candidate_status else None
            reported_objective = result.get("objective_value")
            objective_consistent = (reported_objective is None or check["objective"] is None or
                                    abs(reported_objective - check["objective"]) <= args.objective_tol)
            if candidate_status and (not verification_pass or not objective_consistent):
                status = "NUMERICAL_FAILURE"
            objective = check["objective"] if check["objective"] is not None else result.get("objective_value")
            failure_reason = row.get("failure_reason") or _verification_failure_reason(
                result, check, candidate_status, objective_consistent)
            row.update({k: result.get(k) for k in ("iterations", "lp_solves", "nodes_created", "nodes_processed", "nodes_pruned", "solve_time_ms", "total_time_ms", "dual_residual", "complementarity_residual", "convexity", "hessian_type")})
            row["presolve_fallback"] = result.get("presolve_fallback", False)
            row["presolve_applied_to_solve"] = (
                classification == "LP" and not args.no_presolve and not row["presolve_fallback"]
            )
            row.update({"status": status, "objective_value": objective,
                        "best_primal_bound": _first_number(str(result.get("best_primal_bound")) if result.get("best_primal_bound") is not None else None, str(objective) if objective is not None else None),
                        "best_dual_bound": result.get("best_dual_bound"),
                        "absolute_gap": result.get("absolute_gap"), "relative_gap": result.get("relative_gap"),
                        "primal_residual": check["primal_residual"] if check["primal_residual"] is not None else result.get("primal_residual_reported"),
                        "integer_feasible": check["integer_feasible"], "verification_pass": verification_pass,
                        "verification_time_ms": verify_ms,
                        "failure_reason": failure_reason})
            row["total_time_ms"] = (time.perf_counter_ns() - instance_start) / 1e6
            output = result.get("stdout", "")
            row["parse_time_ms"] = result.get("parse_time_ms", parsed_ms)
            row["presolve_time_ms"] = result.get("presolve_time_ms")
            row["postsolve_time_ms"] = result.get("postsolve_time_ms")
            row["backend"] = result.get("selected_backend") or args.backend
            row["minimum_hessian_eigenvalue_or_classification"] = row["convexity"] or None
            if args.compare_highs:
                reference = _reference_highs(model, args.time_limit)
                our_obj, ref_obj = objective, reference.get("reference_objective")
                obj_difference, obj_match = _objective_comparison(our_obj, ref_obj, args.objective_tol)
                reference.update({"dataset": args.dataset, "instance": str(path), "our_status": status,
                                  "our_objective": our_obj, "objective_difference": obj_difference,
                                  "objective_match": obj_match,
                                  **_runtime_comparison(row["solve_time_ms"], row["total_time_ms"], reference.get("reference_runtime_ms"))})
                external_rows.append(reference)
                row["reference_solver_status"] = reference.get("reference_solver_status")
            if args.compare_backends:
                gpu_supported = classification == "QP" or (classification == "LP" and args.method == "ipm")
                pair = {"dataset": args.dataset, "instance": str(path), "supported": gpu_supported,
                        "gpu_available": gpu["available"], "gpu_device": gpu["device"], "gpu_used": False,
                        "status": "GPU_UNSUPPORTED" if not gpu_supported else "NOT_AVAILABLE" if not gpu["available"] else None,
                        "gpu_reason": "CUDA kernels are supported for IPM LP and QP only" if not gpu_supported else ""}
                if gpu_supported and gpu["available"]:
                    cpu = _invoke(solver, path, method_args, "cpu", args.device, args.time_limit, no_presolve=args.no_presolve)
                    cuda = _invoke(solver, path, method_args, "cuda", args.device, args.time_limit, no_presolve=args.no_presolve)
                    cpu_check = verify_original(model, cpu.get("primal"), args.feasibility_tol, cpu.get("primal_names"))
                    cuda_check = verify_original(model, cuda.get("primal"), args.feasibility_tol, cuda.get("primal_names"))
                    pair.update({"cpu_status": cpu["status"], "gpu_status": cuda["status"],
                                 "cpu_objective": cpu_check["objective"], "gpu_objective": cuda_check["objective"],
                                 "cpu_residual": cpu_check["primal_residual"], "gpu_residual": cuda_check["primal_residual"],
                                 "cpu_time_ms": cpu["solve_time_ms"], "gpu_time_ms": cuda["solve_time_ms"],
                                 "speedup": cpu["solve_time_ms"] / cuda["solve_time_ms"] if cuda["solve_time_ms"] else None,
                                 "gpu_used": cuda["status"] in {"OPTIMAL", "FEASIBLE"} and cuda_check["pass"],
                                 "gpu_kernel_time_ms": None, "cpu_to_gpu_time_ms": None, "gpu_to_cpu_time_ms": None,
                                 "total_gpu_time_ms": cuda["solve_time_ms"]})
                    pair["gpu_reason"] = "measured CLI wall time; CUDA context does not expose per-kernel/transfer event counters"
                elif gpu_supported:
                    pair["gpu_reason"] = "CUDA unavailable or solver was built without a usable CUDA device"
                gpu_rows.append(pair)
            if args.compare_presolve:
                no_pre = _invoke(solver, path, method_args, args.backend, args.device, args.time_limit, no_presolve=True)
                on_pre = _invoke(solver, path, method_args, args.backend, args.device, args.time_limit, no_presolve=False)
                presolve_rows.append({"dataset": args.dataset, "instance": str(path), "original_variables": nvar,
                    "original_constraints": nrow, "original_nonzeros": nnz,
                    "reduced_variables": _int(_field(on_pre["stdout"], "Final variables")),
                    "reduced_constraints": _int(_field(on_pre["stdout"], "Final constraints")),
                    "reduced_nonzeros": None,
                    "presolve_time_ms": _first_number(_field(on_pre["stdout"], "Presolve time ms")),
                    "number_of_reductions": _int(_field(on_pre["stdout"], "Presolve reductions")),
                    "solve_time_without_presolve_ms": no_pre.get("solve_time_ms"),
                    "solve_time_with_presolve_ms": on_pre.get("solve_time_ms"),
                    "total_time_without_presolve_ms": no_pre.get("cli_time_ms"),
                    "total_time_with_presolve_ms": on_pre.get("cli_time_ms"),
                    "without_presolve_status": no_pre.get("status"), "with_presolve_status": on_pre.get("status"),
                    "verification_without_presolve": verify_original(model, no_pre.get("primal"), args.feasibility_tol, no_pre.get("primal_names"))["pass"],
                    "verification_with_presolve": verify_original(model, on_pre.get("primal"), args.feasibility_tol, on_pre.get("primal_names"))["pass"]})
        except (ValueError, OSError, UnicodeError) as exc:
            row.update({"status": "UNSUPPORTED", "failure_reason": str(exc),
                        "parse_time_ms": (time.perf_counter_ns() - parse_start) / 1e6})
        if row["total_time_ms"] is None:
            row["total_time_ms"] = (time.perf_counter_ns() - instance_start) / 1e6
        row["timing"] = {
            "parsing_ms": row.get("parse_time_ms"),
            "model_preparation_ms": row.get("model_preparation_time_ms"),
            "presolve_ms": row.get("presolve_time_ms"),
            "solver_ms": row.get("solve_time_ms"),
            "postsolve_ms": row.get("postsolve_time_ms"),
            "verification_ms": row.get("verification_time_ms"),
            "backend_total_ms": row.get("total_time_ms"),
        }
        rows.append(row)

    for row in rows:
        row.setdefault("timing", {
            "parsing_ms": row.get("parse_time_ms"),
            "model_preparation_ms": row.get("model_preparation_time_ms"),
            "presolve_ms": row.get("presolve_time_ms"),
            "solver_ms": row.get("solve_time_ms"),
            "postsolve_ms": row.get("postsolve_time_ms"),
            "verification_ms": row.get("verification_time_ms"),
            "backend_total_ms": row.get("total_time_ms"),
        })

    metadata = {"timestamp_utc": datetime.now(timezone.utc).isoformat(), "dataset": args.dataset,
                "input_dir": str(root.resolve()), "solver_path": str(solver.resolve()), "solver_version": "Sovereign prototype (working tree)",
                "system": platform.system(), "release": platform.release(), "machine": platform.machine(),
                "python_version": platform.python_version(), "cpu_count": os.cpu_count(),
                "cpu_model": platform.processor() or None, "ram_bytes": _total_memory_bytes(),
                "compiler": build_info.get("build_compiler", "not reported by solver CLI"),
                "build_type": build_info.get("build_type", "not reported by solver CLI"),
                "cpp_standard": build_info.get("cpp_standard"),
                "solver_binary_sha256": hashlib.sha256(solver.read_bytes()).hexdigest(),
                "gpu_info": gpu, "time_limit_seconds": args.time_limit, "tier": args.tier,
                "method": args.method, "backend": args.backend, "feasibility_tolerance": args.feasibility_tol,
                "objective_tolerance": args.objective_tol, "presolve_comparison": args.compare_presolve,
                "external_solver_comparison": args.compare_highs, "cpu_gpu_comparison": args.compare_backends}
    metadata["aggregates"] = _aggregate(rows, external_rows, gpu_rows)
    metadata["presolve_aggregates"] = {
        "instances": len(presolve_rows),
        "verified_without_presolve": sum(r.get("verification_without_presolve") is True for r in presolve_rows),
        "verified_with_presolve": sum(r.get("verification_with_presolve") is True for r in presolve_rows),
        "presolve_faster_solve_count": sum((r.get("solve_time_with_presolve_ms") or float("inf")) < (r.get("solve_time_without_presolve_ms") or 0) for r in presolve_rows),
    }
    # Make comparison artifacts self-describing even when they were not
    # requested or no parsed instance reached the comparison stage.
    if not external_rows:
        requested = args.compare_highs
        external_rows.append({
            "dataset": args.dataset, "instance": None,
            "reference_solver_status": "NOT_RUN" if requested else "NOT_REQUESTED",
            "failure_reason": ("No compatible parsed instance reached the HiGHS comparison stage."
                               if requested else "HiGHS comparison was not requested for this run."),
        })
    if not gpu_rows:
        requested = args.compare_backends
        gpu_rows.append({
            "dataset": args.dataset, "instance": None, "supported": None,
            "status": "NOT_RUN" if requested else "NOT_REQUESTED",
            "gpu_available": gpu["available"], "gpu_device": gpu["device"],
            "gpu_used": False,
            "gpu_reason": ("No supported parsed instance reached the CPU/CUDA comparison stage."
                           if requested else "CPU/CUDA comparison was not requested for this run."),
        })
    out_dir.mkdir(parents=True, exist_ok=True)
    class_files = {"LP": "lp_benchmark", "MILP": "milp_benchmark", "QP": "qp_benchmark"}
    all_json = {"metadata": metadata, "instances": rows}
    (out_dir / f"{args.dataset}_benchmark.json").write_text(json.dumps(all_json, indent=2, allow_nan=False), encoding="utf-8")
    for problem_class, stem in class_files.items():
        subset = [r for r in rows if r["problem_class"] == problem_class]
        _write_csv(out_dir / f"{stem}.csv", subset, CSV_FIELDS)
        (out_dir / f"{stem}.json").write_text(json.dumps({"metadata": metadata, "instances": subset}, indent=2, allow_nan=False), encoding="utf-8")
    _write_csv(out_dir / "external_solver_comparison.csv", external_rows,
               ["dataset", "instance", "reference_solver_status", "reference_status_text", "reference_objective", "reference_runtime_ms", "reference_iterations", "reference_nodes", "reference_gap", "our_status", "our_objective", "objective_difference", "objective_match", "our_solve_time_ms", "our_total_time_ms", "our_runtime_ms", "runtime_ratio", "speedup_vs_highs", "failure_reason"])
    (out_dir / "external_solver_comparison.json").write_text(json.dumps(external_rows, indent=2, allow_nan=False), encoding="utf-8")
    gpu_fields = ["dataset", "instance", "supported", "status", "gpu_available", "gpu_device", "gpu_used", "gpu_reason", "cpu_status", "gpu_status", "cpu_objective", "gpu_objective", "cpu_residual", "gpu_residual", "cpu_time_ms", "gpu_time_ms", "gpu_kernel_time_ms", "cpu_to_gpu_time_ms", "gpu_to_cpu_time_ms", "total_gpu_time_ms", "speedup"]
    _write_csv(out_dir / "cpu_gpu_benchmark.csv", gpu_rows, gpu_fields)
    (out_dir / "cpu_gpu_benchmark.json").write_text(json.dumps(gpu_rows, indent=2, allow_nan=False), encoding="utf-8")
    presolve_fields = ["dataset", "instance", "original_variables", "original_constraints", "original_nonzeros", "reduced_variables", "reduced_constraints", "reduced_nonzeros", "presolve_time_ms", "number_of_reductions", "solve_time_without_presolve_ms", "solve_time_with_presolve_ms", "total_time_without_presolve_ms", "total_time_with_presolve_ms", "without_presolve_status", "with_presolve_status", "verification_without_presolve", "verification_with_presolve"]
    _write_csv(out_dir / "presolve_benchmark.csv", presolve_rows, presolve_fields)
    (out_dir / "presolve_benchmark.json").write_text(json.dumps(presolve_rows, indent=2, allow_nan=False), encoding="utf-8")
    robustness = [r for r in rows if r["status"] not in {"OPTIMAL", "FEASIBLE"} or r["verification_pass"] is False]
    _write_csv(out_dir / "numerical_robustness.csv", robustness, CSV_FIELDS)
    (out_dir / "numerical_robustness.json").write_text(json.dumps(robustness, indent=2, allow_nan=False), encoding="utf-8")
    (out_dir / "benchmark_summary.md").write_text(_summary(args.dataset, rows, metadata), encoding="utf-8")
    return {"metadata": metadata, "rows": rows, "output_dir": str(out_dir.resolve())}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Benchmark Sovereign against local optimization instances")
    parser.add_argument("--dataset", required=True, choices=("netlib", "miplib", "qplib", "mittelmann", "examples", "custom"))
    parser.add_argument("--input", required=True, help="Directory containing locally available instances")
    parser.add_argument("--solver", required=True, help="Path to sovereign_presolve_cli executable")
    parser.add_argument("--method", default="auto", choices=("auto", "revised-simplex", "dual-simplex", "ipm", "qp", "milp", "lp-relaxation", "cutting-plane", "feasibility-pump"))
    parser.add_argument("--backend", default="cpu", choices=("cpu", "cuda", "auto"))
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--tier", choices=("quick", "standard", "full"), default="quick")
    parser.add_argument("--limit", type=int)
    parser.add_argument("--time-limit", type=float, default=60.0)
    parser.add_argument("--objective-tol", type=float, default=1e-6)
    parser.add_argument("--feasibility-tol", type=float, default=1e-7)
    parser.add_argument("--output-dir", default="results")
    parser.add_argument("--no-presolve", action="store_true")
    parser.add_argument("--compare-presolve", action="store_true")
    parser.add_argument("--compare-highs", action="store_true")
    parser.add_argument("--compare-backends", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.time_limit <= 0 or (args.limit is not None and args.limit <= 0):
        print("benchmark error: time limit and instance limit must be positive", file=sys.stderr)
        return 2
    if args.objective_tol < 0 or args.feasibility_tol < 0:
        print("benchmark error: tolerances must be non-negative", file=sys.stderr)
        return 2
    try:
        report = run_benchmark(args)
    except (FileNotFoundError, ValueError) as exc:
        print(f"benchmark error: {exc}", file=sys.stderr)
        return 2
    print(f"Dataset: {args.dataset}")
    print(f"Instances processed: {len(report['rows'])}")
    print(f"Verified successes: {sum(r['status'] in {'OPTIMAL', 'FEASIBLE'} and r['verification_pass'] is True for r in report['rows'])}")
    print(f"Results: {report['output_dir']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
