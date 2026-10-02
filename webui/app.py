from __future__ import annotations

import json
import csv
from functools import lru_cache
import io
import os
import platform
import re
import shutil
import subprocess
import tempfile
import threading
import time
import uuid
from pathlib import Path
from typing import Any

from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, Response, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from sovereign_solver.classification import classify_model
from sovereign_solver.comparison import comparator_availability, solve_comparator
from sovereign_solver.parser import parse_problem_file
from sovereign_solver.qplib import UnsupportedQPLIBFeature, parse_qplib
from webui.solver_policy import SolverPolicy

ROOT = Path(__file__).resolve().parents[1]
STATIC = Path(__file__).resolve().parent / "static"
MAX_UPLOAD_BYTES = 128 * 1024 * 1024
MAX_EXPANDED_MPS_BYTES = 96 * 1024 * 1024
DATASET_ROOT = ROOT / "datasets"
DATASET_PROBLEM_TYPES = ("lp", "milp", "qp")
DATASET_SIZES = ("small", "medium", "large")
HIDDEN_DEMO_DATASET_IDS = {"qp/medium/qplib_8906"}
DATASET_ANALYSIS_CACHE: dict[tuple[str, int, int], dict[str, Any]] = {}
DATASET_CACHE_LOCK = threading.Lock()
SOLUTION_ZERO_TOLERANCE = 1e-8
ALLOWED_EXTENSIONS = {".mps", ".json", ".txt", ".qplib"}
JOBS: dict[str, dict[str, Any]] = {}
JOB_LOCK = threading.Lock()
ACTIVE_SOLVER_PROCESS: subprocess.Popen[str] | None = None
ACTIVE_SOLVER_JOB_ID: str | None = None


def _cpu_solver_path() -> Path:
    candidates = [
        ROOT / "cpp_solver" / "build-route-cpu" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build-route-cpu" / "Release" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build" / "Release" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build" / "sovereign_presolve_cli",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("C++ solver executable was not found. Build cpp_solver before starting the web UI.")


def _cuda_solver_path() -> Path:
    candidates = [
        ROOT / "cpp_solver" / "build-route-cuda" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build-route-cuda" / "Release" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build-cuda" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build-cuda" / "Release" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build-cuda" / "sovereign_presolve_cli",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("CUDA solver executable was not found. Build cpp_solver with SOVEREIGN_ENABLE_CUDA=ON.")


def _solver_path(prefer_cuda: bool = False) -> Path:
    if prefer_cuda:
        try:
            return _cuda_solver_path()
        except FileNotFoundError:
            pass
    return _cpu_solver_path()


def _emps_converter_path() -> Path:
    """Return the locally built Netlib EMPS reference decoder."""
    candidates = [
        ROOT / "cpp_solver" / "build-route-cpu" / "netlib_emps.exe",
        ROOT / "cpp_solver" / "build-route-cpu" / "Release" / "netlib_emps.exe",
        ROOT / "cpp_solver" / "build" / "netlib_emps.exe",
        ROOT / "cpp_solver" / "build" / "Release" / "netlib_emps.exe",
        ROOT / "cpp_solver" / "build" / "netlib_emps",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("The Netlib EMPS decoder is not built. Rebuild cpp_solver to enable compressed MPS uploads.")


def _safe_name(name: str) -> str:
    cleaned = Path(name or "model").name
    if not cleaned or cleaned in {".", ".."}:
        return "model"
    return re.sub(r"[^A-Za-z0-9._-]", "_", cleaned)[:120]


def _is_netlib_emps(path: Path) -> bool:
    """Detect Netlib's checksum-protected EMPS stream without trusting its suffix."""
    try:
        with path.open("rt", encoding="utf-8") as stream:
            significant = [line.strip() for line in stream if line.strip() and not line.lstrip().startswith("*")][:4]
    except (OSError, UnicodeError):
        return False
    if len(significant) < 3 or not significant[0].upper().startswith("NAME"):
        return False
    statistics = significant[1].split() + significant[2].split()
    return len(significant[1].split()) == 8 and len(significant[2].split()) == 3 and all(token.isdecimal() for token in statistics)


def _is_qplib(path: Path) -> bool:
    """Detect QPLIB from contents, including browser-renamed .qplib.txt uploads."""
    try:
        with path.open("rt", encoding="utf-8") as stream:
            for line in stream:
                candidate = line.split("#", 1)[0].strip()
                if candidate:
                    return candidate.upper().startswith("QPLIB")
    except (OSError, UnicodeError):
        return False
    return False


def _convert_qplib_to_json(source: Path, directory: Path) -> Path:
    """Create a job-local canonical model so the C++ executable sees QPLIB data."""
    destination = directory / "converted_from_qplib.json"
    try:
        payload = parse_qplib(source.read_text(encoding="utf-8"))
        destination.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")
    except UnsupportedQPLIBFeature as exc:
        destination.unlink(missing_ok=True)
        raise UnsupportedQPLIBFeature(str(exc)) from exc
    except (OSError, UnicodeError, ValueError) as exc:
        destination.unlink(missing_ok=True)
        raise ValueError(f"QPLIB parsing failed: {exc}") from exc
    return destination


def _expand_netlib_emps(source: Path, directory: Path) -> Path:
    """Expand EMPS with the checked-in Netlib reference decoder into a job-local MPS file."""
    destination = directory / "expanded_from_emps.mps"
    try:
        converter = _emps_converter_path()
        with destination.open("wt", encoding="utf-8", newline="\n") as output:
            process = subprocess.run(
                [str(converter), str(source)],
                cwd=directory,
                stdin=subprocess.DEVNULL,
                stdout=output,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=30,
                check=False,
            )
    except (OSError, subprocess.TimeoutExpired, FileNotFoundError) as exc:
        destination.unlink(missing_ok=True)
        raise ValueError(f"Could not expand Netlib EMPS input: {exc}") from exc
    if process.returncode != 0 or not destination.is_file() or destination.stat().st_size == 0:
        detail = (process.stderr or "decoder returned no MPS output").strip()
        destination.unlink(missing_ok=True)
        raise ValueError(f"Netlib EMPS decoding failed: {detail[-1000:]}")
    if destination.stat().st_size > MAX_EXPANDED_MPS_BYTES:
        destination.unlink(missing_ok=True)
        raise ValueError(f"Expanded Netlib EMPS model exceeds the {MAX_EXPANDED_MPS_BYTES // (1024 * 1024)} MB safety limit.")
    return destination


def _format_bound(value: Any) -> float | None:
    return None if value is None else float(value)


def _analyze_large_mps(path: Path) -> dict[str, Any]:
    """Stream MPS structure for large uploads without building a second full Model."""
    section = ""
    name = path.stem
    rows: list[tuple[str, str]] = []
    row_ids: dict[str, int] = {}
    variables: list[list[Any]] = []
    variable_ids: dict[str, int] = {}
    row_entries: list[list[tuple[int, float]]] = []
    rhs_set = range_set = bounds_set = None
    ranges: set[int] = set()
    integer_block = False
    objective_row = None
    objective_sense = "minimize"
    current_name: str | None = None
    current_index = -1
    current_values: dict[int, float] = {}

    def ensure_variable(variable_name: str) -> int:
        found = variable_ids.get(variable_name)
        if found is not None:
            return found
        index = len(variables)
        variable_ids[variable_name] = index
        variables.append([variable_name, "continuous", 0.0, None])
        return index

    def flush_column() -> None:
        for row_index, coefficient in current_values.items():
            if coefficient != 0.0:
                row_entries[row_index].append((current_index, coefficient))
        current_values.clear()

    with path.open("rt", encoding="utf-8", errors="strict") as stream:
        for raw in stream:
            if not raw.strip() or raw.lstrip().startswith("*"):
                continue
            tokens = raw.split()
            head = tokens[0].upper()
            if head in {"NAME", "ROWS", "COLUMNS", "RHS", "RANGES", "BOUNDS", "OBJSENSE", "ENDATA"}:
                if section == "COLUMNS" and head != "COLUMNS":
                    flush_column()
                section = head
                if head == "NAME" and len(tokens) > 1:
                    name = tokens[1]
                elif head == "ENDATA":
                    break
                continue
            if section == "ROWS":
                if len(tokens) < 2:
                    raise ValueError(f"Invalid MPS ROWS record: {raw.strip()}")
                row_type, row_name = tokens[0].upper(), tokens[1]
                row_ids[row_name] = len(rows)
                rows.append((row_name, row_type))
                if row_type == "N" and objective_row is None:
                    objective_row = row_name
                    row_entries.append([])
                else:
                    row_entries.append([])
            elif section == "COLUMNS":
                if len(tokens) >= 3 and tokens[1].upper() == "'MARKER'":
                    integer_block = "INTORG" in tokens[2].upper()
                    continue
                if len(tokens) < 3 or (len(tokens) - 1) % 2:
                    raise ValueError(f"Invalid MPS COLUMNS record: {raw.strip()}")
                variable_name = tokens[0]
                if current_name != variable_name:
                    flush_column()
                    current_name = variable_name
                    current_index = ensure_variable(variable_name)
                if integer_block and variables[current_index][1] == "continuous":
                    variables[current_index][1] = "integer"
                for index in range(1, len(tokens), 2):
                    row_name = tokens[index]
                    row_index = row_ids.get(row_name)
                    if row_index is None:
                        raise ValueError(f"MPS column references unknown row '{row_name}'.")
                    coefficient = float(tokens[index + 1].replace("D", "E").replace("d", "e"))
                    if rows[row_index][1] != "N":
                        current_values[row_index] = current_values.get(row_index, 0.0) + coefficient
            elif section == "RHS":
                if len(tokens) < 3:
                    raise ValueError(f"Invalid MPS RHS record: {raw.strip()}")
                rhs_set = rhs_set or tokens[0]
            elif section == "RANGES":
                if len(tokens) < 3:
                    raise ValueError(f"Invalid MPS RANGES record: {raw.strip()}")
                range_set = range_set or tokens[0]
                if tokens[0] == range_set:
                    for index in range(1, len(tokens), 2):
                        row_index = row_ids.get(tokens[index])
                        if row_index is not None:
                            ranges.add(row_index)
            elif section == "BOUNDS":
                if len(tokens) < 3:
                    raise ValueError(f"Invalid MPS BOUNDS record: {raw.strip()}")
                bounds_set = bounds_set or tokens[1]
                if tokens[1] != bounds_set:
                    continue
                variable_index = ensure_variable(tokens[2])
                bound_type = tokens[0].upper()
                bound_value = float(tokens[3].replace("D", "E").replace("d", "e")) if len(tokens) > 3 else None
                variable = variables[variable_index]
                if bound_type == "BV":
                    variable[1], variable[2], variable[3] = "binary", 0.0, 1.0
                elif bound_type in {"LI", "UI"}:
                    variable[1] = "integer"
                    if bound_type == "LI": variable[2] = bound_value
                    else: variable[3] = bound_value
                elif bound_type == "LO": variable[2] = bound_value
                elif bound_type == "UP": variable[3] = bound_value
                elif bound_type == "FX": variable[2] = variable[3] = bound_value
                elif bound_type in {"FR", "MI"}: variable[2] = None
                elif bound_type == "PL": variable[3] = None
                else: raise ValueError(f"Unsupported MPS bound type '{bound_type}'.")
            elif section == "OBJSENSE":
                sense = tokens[0].upper()
                if sense in {"MAX", "MAXIMIZE"}: objective_sense = "maximize"
                elif sense in {"MIN", "MINIMIZE"}: objective_sense = "minimize"

    if section == "COLUMNS":
        flush_column()
    if not rows or objective_row is None or not variables:
        raise ValueError("Incomplete MPS file: NAME, ROWS, COLUMNS, and ENDATA are required.")
    constraint_rows = [index for index, (_, kind) in enumerate(rows) if kind != "N"]
    expanded_rows: list[list[tuple[int, float]]] = []
    for index in constraint_rows:
        expanded_rows.append(row_entries[index])
        if index in ranges:
            expanded_rows.append(row_entries[index])
    constraints_count = len(expanded_rows)
    variable_count = len(variables)
    linear_nonzeros = 0
    display_rows = min(50, constraints_count) if constraints_count else 0
    display_columns = min(50, variable_count) if variable_count else 0
    buckets: dict[tuple[int, int], int] = {}
    if constraints_count and variable_count:
        for row_index, entries in enumerate(expanded_rows):
            display_row = row_index * display_rows // constraints_count
            for variable_index, coefficient in entries:
                linear_nonzeros += 1
                display_column = variable_index * display_columns // variable_count
                key = display_row, display_column
                buckets[key] = buckets.get(key, 0) + 1
    type_counts = {kind: sum(variable[1] == kind for variable in variables) for kind in ("continuous", "integer", "binary")}
    bounded = sum(variable[2] is not None or variable[3] is not None for variable in variables)
    denominator = variable_count * constraints_count
    sparsity = (1 - linear_nonzeros / denominator) * 100 if denominator else 100.0
    return {
        "name": name, "problem_type": "MILP" if type_counts["integer"] or type_counts["binary"] else "LP",
        "classification_reason": "Large MPS with discrete variables." if type_counts["integer"] or type_counts["binary"] else "Large linear MPS.",
        "format": "MPS", "file_name": path.name, "file_size": path.stat().st_size,
        "variables": variable_count, "constraints": constraints_count, "nonzeros": linear_nonzeros,
        "linear_nonzeros": linear_nonzeros, "quadratic_nonzeros": 0, "sparsity": sparsity,
        "types": type_counts, "bounded_variables": bounded, "objective_sense": objective_sense,
        "matrix": {"rows":constraints_count,"columns":variable_count,"points":[],"buckets":[[r,c,n] for (r,c),n in buckets.items()],"display_rows":display_rows,"display_columns":display_columns,"aggregated":display_rows<constraints_count or display_columns<variable_count},
        "variable_metadata": [tuple(variable) for variable in variables],
    }


def analyze_model(path: Path) -> dict[str, Any]:
    if path.suffix.lower() == ".mps" and path.stat().st_size >= 8 * 1024 * 1024:
        return _analyze_large_mps(path)
    model = parse_problem_file(str(path))
    classification = classify_model(model)
    variables = model.variables
    constraints = model.constraints
    linear_nonzeros = sum(len(row.coefficients) for row in constraints)
    quadratic_nonzeros = len(model.quadratic_terms)
    nonzeros = linear_nonzeros + quadratic_nonzeros
    variable_count = len(variables)
    constraint_count = len(constraints)
    denominator = variable_count * constraint_count
    sparsity = (1 - linear_nonzeros / denominator) * 100 if denominator else 100.0
    variable_index = {variable.name: index for index, variable in enumerate(variables)}
    # Visualization-only aggregation. It reads every parsed coefficient, while
    # keeping the browser payload bounded for very large matrices.
    display_rows = min(50, constraint_count) if constraint_count else 0
    display_columns = min(50, variable_count) if variable_count else 0
    buckets: dict[tuple[int, int], int] = {}
    points: list[list[int]] = []
    sample_limit = 5000
    for row_index, row in enumerate(constraints):
        for variable in row.coefficients:
            variable_column = variable_index[variable]
            if display_rows and display_columns:
                bucket = (row_index * display_rows // constraint_count, variable_column * display_columns // variable_count)
                buckets[bucket] = buckets.get(bucket, 0) + 1
            if len(points) >= sample_limit:
                continue
            points.append([row_index, variable_column])
    type_counts = {kind: sum(v.type == kind for v in variables) for kind in ("continuous", "integer", "binary")}
    bounded = sum(variable.name in model.bounds for variable in variables)
    return {
        "name": model.name,
        "problem_type": classification.problem_type,
        "classification_reason": classification.reason,
        "format": path.suffix.lower().removeprefix(".").upper(),
        "file_name": path.name,
        "file_size": path.stat().st_size,
        "variables": variable_count,
        "constraints": constraint_count,
        "nonzeros": nonzeros,
        "linear_nonzeros": linear_nonzeros,
        "quadratic_nonzeros": quadratic_nonzeros,
        "sparsity": sparsity,
        "types": type_counts,
        "bounded_variables": bounded,
        "objective_sense": model.objective_sense,
        "matrix": {
            "rows": constraint_count,
            "columns": variable_count,
            "points": points,
            "buckets": [[row, column, count] for (row, column), count in buckets.items()],
            "display_rows": display_rows,
            "display_columns": display_columns,
            "aggregated": display_rows < constraint_count or display_columns < variable_count,
        },
        "variable_metadata": [
            {"name": v.name, "type": v.type, "lower": _format_bound(model.bounds.get(v.name, (None, None))[0]), "upper": _format_bound(model.bounds.get(v.name, (None, None))[1])}
            for v in variables
        ],
    }


def _public_analysis(analysis: dict[str, Any]) -> dict[str, Any]:
    """Keep per-variable metadata and sampled points inside the server job."""
    public = {key: value for key, value in analysis.items() if key != "variable_metadata"}
    matrix = dict(public.get("matrix") or {})
    matrix.pop("points", None)
    public["matrix"] = matrix
    return public


def _prepare_model(model_path: Path, directory: Path) -> tuple[Path, dict[str, Any]]:
    """Return the actual standard-MPS/JSON input consumed by the solver and its analysis."""
    conversion = None
    if _is_netlib_emps(model_path):
        solver_path = _expand_netlib_emps(model_path, directory)
        conversion = ("NETLIB EMPS → MPS", "Expanded locally with the checked-in Netlib EMPS decoder before analysis and solving.")
    elif _is_qplib(model_path):
        solver_path = _convert_qplib_to_json(model_path, directory)
        conversion = ("QPLIB → JSON", "Parsed locally as QPLIB and converted into the solver's canonical JSON model before analysis and solving.")
    else:
        solver_path = model_path
    summary = analyze_model(solver_path)
    if conversion:
        summary.update({
            "file_name": model_path.name,
            "file_size": model_path.stat().st_size,
            "format": conversion[0],
            "conversion": conversion[1],
        })
    return solver_path, summary


def _dataset_entries(problem_type: str | None = None) -> list[dict[str, Any]]:
    """Discover checked-in datasets from datasets/<type>/<size>/<file>."""
    entries: list[dict[str, Any]] = []
    problem_types = (problem_type,) if problem_type else DATASET_PROBLEM_TYPES
    for current_type in problem_types:
        if current_type not in DATASET_PROBLEM_TYPES:
            continue
        for size in DATASET_SIZES:
            directory = DATASET_ROOT / current_type / size
            if not directory.is_dir():
                continue
            for source in sorted(directory.iterdir(), key=lambda item: item.name.lower()):
                if not source.is_file() or source.suffix.lower() not in ALLOWED_EXTENSIONS:
                    continue
                relative_id = source.relative_to(DATASET_ROOT).with_suffix("").as_posix()
                if relative_id.lower() in HIDDEN_DEMO_DATASET_IDS:
                    continue
                entries.append({
                    "id": relative_id,
                    "problem_type": current_type.upper(),
                    "size": size,
                    "name": source.stem.upper(),
                    "file_name": source.name,
                    "path": source,
                })
    return entries


def _analyze_job_file(model_path: Path, directory: Path) -> tuple[str, dict[str, Any]]:
    preparation_started = time.perf_counter()
    solver_path, summary = _prepare_model(model_path, directory)
    preparation_time_ms = (time.perf_counter() - preparation_started) * 1000
    job_id = uuid.uuid4().hex
    JOBS[job_id] = {
        "directory": directory,
        "path": solver_path,
        "source_path": model_path,
        "analysis": summary,
        "preparation_time_ms": preparation_time_ms,
        "state": "analyzed",
    }
    return job_id, summary


def _capture(pattern: str, output: str, default: Any = None, cast: type = str) -> Any:
    match = re.search(pattern, output, re.MULTILINE | re.IGNORECASE)
    if not match:
        return default
    try:
        return cast(match.group(1).strip())
    except (ValueError, TypeError):
        return default


def parse_solver_output(output: str, total_ms: float) -> dict[str, Any]:
    status = _capture(r"^Status:\s*(.+)$", output, "FAILED")
    if status == "FAILED":
        status = _capture(r"^SOLVER Status:\s*(.+)$", output, "FAILED")
    verification = _capture(r"^Verification:\s*(.+)$", output, "N/A")
    timings = {
        "parse_time_ms": _capture(r"^Parse time ms:\s*([^\r\n]+)$", output, None, float),
        "presolve_time_ms": _capture(r"^Presolve time ms:\s*([^\r\n]+)$", output, None, float),
        "standardization_time_ms": _capture(r"^Standardization time ms:\s*([^\r\n]+)$", output, None, float),
        "solve_pipeline_time_ms": _capture(r"^Solve pipeline time ms:\s*([^\r\n]+)$", output, None, float),
        "solver_time_ms": _capture(r"^Solve time ms:\s*([^\r\n]+)$", output, None, float),
        "postsolve_time_ms": _capture(r"^Postsolve time ms:\s*([^\r\n]+)$", output, None, float),
        "verification_time_ms": _capture(r"^Verification time ms:\s*([^\r\n]+)$", output, None, float),
        "backend_total_time_ms": total_ms,
        "model_preparation_time_ms": None,
    }
    timing = {
        "parsing_ms": timings["parse_time_ms"],
        "model_preparation_ms": timings["model_preparation_time_ms"],
        "presolve_ms": timings["presolve_time_ms"],
        "standardization_ms": timings["standardization_time_ms"],
        "solver_ms": timings["solver_time_ms"],
        "postsolve_ms": timings["postsolve_time_ms"],
        "verification_ms": timings["verification_time_ms"],
        "backend_total_ms": timings["backend_total_time_ms"],
    }
    names = (_capture(r"^Primal Names:\s*(.*)$", output, "") or "").split()
    primal_line = _capture(r"^Primal:\s*(.*)$", output, "") or ""
    values: list[float] = []
    for token in primal_line.split():
        try:
            values.append(float(token))
        except ValueError:
            pass
    variables = [
        {"name": name, "value": values[index] if index < len(values) else None}
        for index, name in enumerate(names)
    ]
    sparse_line = _capture(r"^Primal Sparse:\s*(.*)$", output, None)
    sparse_variables: list[dict[str, Any]] | None = None
    if sparse_line is not None:
        sparse_variables = []
        for token in sparse_line.split():
            if "=" not in token:
                continue
            index_text, value_text = token.split("=", 1)
            try:
                sparse_variables.append({"index": int(index_text), "value": float(value_text)})
            except ValueError:
                continue
        variables = sparse_variables
    presolve = {
        "termination_reason": _capture(r"^Presolve termination:\s*([^\r\n]+)$", output, None),
        "time_budget_ms": _capture(r"^Presolve time budget ms:\s*([^\r\n]+)$", output, None, float),
        "before_variables": _capture(r"^Variables:\s*(\d+)$", output, None, int),
        "before_constraints": _capture(r"^Constraints:\s*(\d+)$", output, None, int),
        "after_variables": _capture(r"^Final variables:\s*(\d+)$", output, None, int),
        "after_constraints": _capture(r"^Final constraints:\s*(\d+)$", output, None, int),
        "reductions": _capture(r"^Presolve reductions:\s*(\d+)$", output, None, int),
        "before_nonzeros": _capture(r"^Nonzeros:\s*(\d+)$", output, None, int),
        "standardized_rows": _capture(r"^Standardized rows:\s*(\d+)$", output, None, int),
        "standardized_columns": _capture(r"^Standardized columns:\s*(\d+)$", output, None, int),
        "standardized_nonzeros": _capture(r"^Standardized nonzeros:\s*(\d+)$", output, None, int),
        "pass_stats": [line for line in re.findall(r"(?im)^Presolve pass:\s*[^\r\n]+$", output)],
    }
    metrics = {
        "objective": _capture(r"^Objective:\s*([^\r\n]+)$", output, None, float),
        "iterations": _capture(r"^Iterations:\s*(\d+)$", output, None, int),
        "nodes_created": _capture(r"^Nodes Created:\s*(\d+)$", output, None, int),
        "nodes_processed": _capture(r"^Nodes Processed:\s*(\d+)$", output, None, int),
        "nodes_pruned": _capture(r"^Nodes Pruned:\s*(\d+)$", output, None, int),
        "lp_solves": _capture(r"^LP Solves:\s*(\d+)$", output, None, int),
        "lp_iterations": _capture(r"^LP Iterations:\s*(\d+)$", output, None, int),
        "primal_bound": _capture(r"^Primal Bound:\s*([^\r\n]+)$", output, None, float),
        "dual_bound": _capture(r"^Dual Bound:\s*([^\r\n]+)$", output, None, float),
        "absolute_gap": _capture(r"^Absolute Gap:\s*([^\r\n]+)$", output, None, float),
        "relative_gap": _capture(r"^Relative Gap:\s*([^\r\n]+)$", output, None, float),
        "primal_residual": _capture(r"^Primal residual:\s*([^\r\n]+)$", output, None, float),
        "dual_residual": _capture(r"^Dual residual:\s*([^\r\n]+)$", output, None, float),
        "feasibility": _capture(r"^Feasibility:\s*([^\r\n]+)$", output, None, float),
        "iteration_limit": _capture(r"^Iteration limit:\s*(.+)$", output, None),
        "attempt_count": _capture(r"^Attempt count:\s*(\d+)$", output, None, int),
        "fallback_reason": _capture(r"^Fallback reason:\s*([^\r\n]+)$", output, None),
        "estimated_dense_memory_bytes": _capture(r"^Estimated dense memory bytes:\s*([^\r\n]+)$", output, None, float),
        "dense_memory_budget_bytes": _capture(r"^Dense memory budget bytes:\s*([^\r\n]+)$", output, None, int),
        "dense_memory_guard_triggered": _capture(r"^Dense memory guard:\s*(TRIGGERED|NOT TRIGGERED)$", output, None),
        "sparse_pricing_ms": _capture(r"^Sparse pricing time ms:\s*([^\r\n]+)$", output, None, float),
        "sparse_basis_solve_ms": _capture(r"^Sparse basis solve time ms:\s*([^\r\n]+)$", output, None, float),
        "sparse_devex_ms": _capture(r"^Sparse Devex time ms:\s*([^\r\n]+)$", output, None, float),
        "sparse_factorization_ms": _capture(r"^Sparse factorization time ms:\s*([^\r\n]+)$", output, None, float),
        "sparse_ratio_test_ms": _capture(r"^Sparse ratio test time ms:\s*([^\r\n]+)$", output, None, float),
        "sparse_lexicographic_ms": _capture(r"^Sparse lexicographic time ms:\s*([^\r\n]+)$", output, None, float),
        "sparse_refactorizations": _capture(r"^Sparse refactorizations:\s*(\d+)$", output, None, int),
        "sparse_pivots": _capture(r"^Sparse pivots:\s*(\d+)$", output, None, int),
        "sparse_lexicographic_solves": _capture(r"^Sparse lexicographic solves:\s*(\d+)$", output, None, int),
        "sparse_bland_fallback_triggered": _capture(r"^Sparse Bland fallback:\s*(YES|NO)$", output, None) == "YES",
        "backend": _capture(r"^Backend:\s*(.+)$", output, "cpu"),
        "backend_reason": _capture(r"^Backend reason:\s*(.+)$", output, None),
        "method": _capture(r"^METHOD:\s*(.+)$", output, None) or _capture(r"^Method:\s*(.+)$", output, None),
        "selected_algorithm": _capture(r"^Selected algorithm:\s*(.+)$", output, None),
        "resolved_algorithm": _capture(r"^Resolved algorithm:\s*(.+)$", output, None),
        "message": _capture(r"^Message:[ \t]*([^\r\n]*)$", output, None),
        "convexity": _capture(r"^Hessian:\s*(.+)$", output, None),
    }
    return {"status": status, "verification": verification, "timings": timings, "timing": timing, "metrics": metrics, "presolve": presolve, "variables": variables, "sparse_primal": sparse_variables is not None, "raw_log": output[-16000:]}


def _record_execution_trace(result: dict[str, Any], job: dict[str, Any], configuration: dict[str, Any]) -> None:
    metrics = result.setdefault("metrics", {})
    model_size = str(configuration.get("model_size") or SolverPolicy.limits_for(job["analysis"])[0].name)
    problem_type = str(job["analysis"].get("problem_type", "UNKNOWN")).upper()
    selected = str(configuration.get("selected_algorithm") or configuration.get("method") or "revised-simplex")
    resolved = str(metrics.get("resolved_algorithm") or metrics.get("method") or selected)
    backend = str(metrics.get("backend") or configuration.get("backend") or "unknown").lower()
    configuration.update({
        "problem_type": problem_type,
        "model_size": model_size,
        "user_selected_algorithm": selected,
        "resolved_algorithm": resolved,
        "backend": backend,
        "resolved_backend": backend,
        "time_limit_seconds": int(configuration.get("time_limit_seconds", 0)),
    })
    trace = {
        "problem_type": problem_type,
        "model_size": model_size,
        "user_selected_algorithm": selected,
        "resolved_algorithm": resolved,
        "resolved_backend": backend,
        "time_limit_seconds": configuration["time_limit_seconds"],
    }
    result["execution_trace"] = trace
    metrics.update({"problem_type": problem_type, "model_size": model_size,
                    "user_selected_algorithm": selected, "resolved_algorithm": resolved,
                    "resolved_backend": backend, "time_limit_seconds": trace["time_limit_seconds"]})
    trace_text = "\n".join(f"{key}: {value}" for key, value in trace.items())
    raw_log = str(result.get("raw_log") or "")
    result["raw_log"] = f"EXECUTION ROUTING TRACE\n{trace_text}\n\n{raw_log}"[-16000:]


def _publish_timing_contract(result: dict[str, Any], backend_total_ms: float | None = None) -> None:
    """Keep public timing data and legacy UI fields on one measured source."""
    timings = result.setdefault("timings", {})
    if backend_total_ms is not None:
        timings["backend_total_time_ms"] = backend_total_ms
    result["timing"] = {
        "parsing_ms": timings.get("parse_time_ms"),
        "model_preparation_ms": timings.get("model_preparation_time_ms"),
        "presolve_ms": timings.get("presolve_time_ms"),
        "standardization_ms": timings.get("standardization_time_ms"),
        "solver_ms": timings.get("solver_time_ms"),
        "postsolve_ms": timings.get("postsolve_time_ms"),
        "verification_ms": timings.get("verification_time_ms"),
        "backend_total_ms": timings.get("backend_total_time_ms"),
    }


def _solution_variables(parsed: list[dict[str, Any]], metadata: list[dict[str, Any]], sparse: bool = False) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Map solver values to original variable names without turning missing data into zero."""
    def fields(meta: Any) -> tuple[str, str, float | None, float | None]:
        if isinstance(meta, tuple):
            return str(meta[0]), str(meta[1]), meta[2], meta[3]
        return str(meta["name"]), str(meta.get("type") or "continuous"), meta.get("lower"), meta.get("upper")

    if sparse:
        returned = {int(row["index"]): float(row["value"]) for row in parsed if row.get("index") is not None}
        rows: list[dict[str, Any]] = []
        integer_count = binary_count = at_bound = fractional = 0
        for index, meta in enumerate(metadata):
            value = returned.get(index, 0.0)
            variable_name, variable_type, lower, upper = fields(meta)
            integer_count += variable_type == "integer"
            binary_count += variable_type == "binary"
            is_at_bound = (lower is not None and abs(value-float(lower))<=SOLUTION_ZERO_TOLERANCE) or (upper is not None and abs(value-float(upper))<=SOLUTION_ZERO_TOLERANCE)
            is_fractional = variable_type in {"integer", "binary"} and abs(value-round(value))>SOLUTION_ZERO_TOLERANCE
            at_bound += is_at_bound
            fractional += is_fractional
            if abs(value) > SOLUTION_ZERO_TOLERANCE:
                rows.append({"name":variable_name,"value":value,"type":variable_type,"lower":lower,"upper":upper,"at_bound":bool(is_at_bound),"fractional":bool(is_fractional)})
        total = len(metadata)
        return rows, {"total":total,"available":total,"missing":0,"nonzero":len(rows),"zero":total-len(rows),"integer":integer_count,"binary":binary_count,"at_bound":at_bound,"fractional":fractional,"zero_tolerance":SOLUTION_ZERO_TOLERANCE,"sparse":True}
    if len(metadata) > 100_000 and not parsed:
        return [], {"total":len(metadata),"available":0,"missing":len(metadata),"nonzero":0,"zero":0,"integer":sum(fields(m)[1]=="integer" for m in metadata),"binary":sum(fields(m)[1]=="binary" for m in metadata),"at_bound":0,"fractional":0,"zero_tolerance":SOLUTION_ZERO_TOLERANCE}
    returned = {str(row.get("name")): row.get("value") for row in parsed if row.get("name") is not None}
    rows: list[dict[str, Any]] = []
    for meta in metadata:
        name, variable_type, lower, upper = fields(meta)
        value = returned.get(name)
        numeric_value = isinstance(value, (int, float)) and not isinstance(value, bool)
        at_lower = numeric_value and lower is not None and abs(float(value) - float(lower)) <= SOLUTION_ZERO_TOLERANCE
        at_upper = numeric_value and upper is not None and abs(float(value) - float(upper)) <= SOLUTION_ZERO_TOLERANCE
        fractional = numeric_value and variable_type in {"integer", "binary"} and abs(float(value) - round(float(value))) > SOLUTION_ZERO_TOLERANCE
        rows.append({
            "name": name,
            "value": value if numeric_value else None,
            "type": variable_type,
            "lower": lower,
            "upper": upper,
            "at_bound": bool(at_lower or at_upper),
            "fractional": fractional,
        })
    total = len(rows)
    available = [row for row in rows if row["value"] is not None]
    nonzero = sum(abs(float(row["value"])) > SOLUTION_ZERO_TOLERANCE for row in available)
    types = {kind: sum(row["type"] == kind for row in rows) for kind in ("integer", "binary")}
    return rows, {
        "total": total,
        "available": len(available),
        "missing": total - len(available),
        "nonzero": nonzero,
        "zero": sum(abs(float(row["value"])) <= SOLUTION_ZERO_TOLERANCE for row in available),
        "integer": types["integer"],
        "binary": types["binary"],
        "at_bound": sum(row["at_bound"] for row in rows),
        "fractional": sum(row["fractional"] for row in rows),
        "zero_tolerance": SOLUTION_ZERO_TOLERANCE,
    }


@lru_cache(maxsize=1)
def device_info() -> dict[str, Any]:
    try:
        completed = subprocess.run([str(_solver_path(prefer_cuda=True)), "--device-info"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10, check=False)
        text = completed.stdout.strip()
    except (OSError, subprocess.TimeoutExpired, FileNotFoundError) as exc:
        text = f"CUDA status unavailable: {exc}"
    cuda_available = bool(re.search(r"CUDA Available:\s*YES", text, re.I))
    gpu_count = _capture(r"GPU Count:\s*(\d+)", text, 0, int)
    cuda_backend_compiled = "CUDA Runtime:" in text
    if cuda_available:
        cuda_reason = "CUDA device and runtime are available for implemented numerical operations."
    elif cuda_backend_compiled:
        cuda_reason = "CUDA backend is compiled, but no compatible NVIDIA device is available."
    else:
        cuda_reason = "CUDA backend not compiled; CPU fallback is active."
    return {
        "cpu": platform.processor() or platform.machine() or "CPU information unavailable",
        "threads": os.cpu_count() or 1,
        "cuda_available": cuda_available,
        "gpu_count": gpu_count,
        "gpu_name": _capture(r"(?:GPU(?: Name)?|Device):\s*(.+)$", text, None),
        "cuda_runtime": _capture(r"CUDA Runtime:\s*(.+)$", text, None),
        "cuda_backend_compiled": cuda_backend_compiled,
        "cuda_reason": cuda_reason,
        "raw": text,
    }


class SolveRequest(BaseModel):
    job_id: str
    method: str = Field(default="auto", pattern="^(auto|revised-simplex|dual-simplex|ipm|qp|milp|lp-relaxation|cutting-plane|feasibility-pump)$")
    selected_algorithm: str | None = Field(default=None, pattern="^(auto|revised-simplex|dual-simplex|ipm|qp|milp|cutting-plane|feasibility-pump)$")
    problem_type: str | None = Field(default=None, pattern="^(LP|MILP|QP)$")
    backend: str = Field(default="auto", pattern="^(auto|cpu|cuda)$")
    presolve: bool = True
    max_iterations: int = Field(default=10000, ge=0, le=10_000_000)
    max_nodes: int = Field(default=10000, ge=0, le=10_000_000)
    time_limit_seconds: int | None = Field(default=None, ge=0, le=3600)


class AutoSolveRequest(BaseModel):
    job_id: str


class RoutePreviewRequest(BaseModel):
    problem_type: str = Field(pattern="^(LP|MILP|QP)$")
    selected_algorithm: str = Field(pattern="^(revised-simplex|dual-simplex|ipm|qp|milp|cutting-plane|feasibility-pump)$")
    backend: str = Field(default="auto", pattern="^auto$")


class ActiveModelCompareRequest(BaseModel):
    model_id: str = Field(min_length=1, max_length=64)
    time_limit_seconds: int = Field(default=60, ge=1, le=3600)


app = FastAPI(title="Sovereign Optimization API", version="1.0")
app.add_middleware(CORSMiddleware, allow_origins=["http://localhost:8000", "http://127.0.0.1:8000"], allow_methods=["*"], allow_headers=["*"])


@app.get("/api/health")
def health() -> dict[str, Any]:
    try:
        solver = str(_solver_path(prefer_cuda=True))
        ready = True
    except FileNotFoundError as exc:
        solver, ready = str(exc), False
    return {"status": "ready" if ready else "degraded", "solver": solver}


@app.get("/api/device")
def device() -> dict[str, Any]:
    return device_info()


@app.get("/api/capabilities")
def capabilities() -> dict[str, Any]:
    info = device_info()
    return {
        "formats": sorted(extension.removeprefix(".") for extension in ALLOWED_EXTENSIONS),
        "problem_types": ["LP", "QP", "MILP"],
        "methods": {
        "LP": ["revised-simplex", "dual-simplex", "ipm"],
            "QP": ["qp"],
            "MILP": ["milp", "cutting-plane", "feasibility-pump", "lp-relaxation"],
        },
        "cuda_available": info["cuda_available"],
        "cuda_reason": info["cuda_reason"],
        "automatic_execution_policy": {
            "time_limits_seconds": [60, 120, 300],
            "time_limits_by_model_size": {"SMALL": 60, "MEDIUM": 120, "LARGE": 300},
            "lp_iteration_limits": [0],
            "qp_iteration_limits": [200],
            "milp_lp_iteration_limits": [10000, 25000, 50000],
            "milp_node_limits": [10000, 100000, 500000],
            "zero_means_unlimited": True,
        },
        "max_iterations": {"default": 10000, "zero_means_unlimited": True},
        "max_nodes": {"default": 10000, "zero_means_unlimited": True},
    }


@app.post("/api/analyze")
async def analyze(file: UploadFile = File(...)) -> dict[str, Any]:
    suffix = Path(file.filename or "").suffix.lower()
    if suffix not in ALLOWED_EXTENSIONS:
        raise HTTPException(415, f"Unsupported file format. Supported formats: {', '.join(sorted(ALLOWED_EXTENSIONS))}.")
    payload = await file.read(MAX_UPLOAD_BYTES + 1)
    if len(payload) > MAX_UPLOAD_BYTES:
        raise HTTPException(413, f"File exceeds the {MAX_UPLOAD_BYTES // (1024 * 1024)} MB upload limit.")
    job_id = uuid.uuid4().hex
    directory = Path(tempfile.mkdtemp(prefix=f"sovereign-{job_id[:8]}-"))
    model_path = directory / _safe_name(file.filename or f"model{suffix}")
    model_path.write_bytes(payload)
    try:
        job_id, summary = _analyze_job_file(model_path, directory)
    except UnsupportedQPLIBFeature as exc:
        shutil.rmtree(directory, ignore_errors=True)
        raise HTTPException(422, f"Unsupported QPLIB model: {exc}") from exc
    except ValueError as exc:
        shutil.rmtree(directory, ignore_errors=True)
        raise HTTPException(422, f"Model parsing failed: {exc}") from exc
    return {"job_id": job_id, "analysis": _public_analysis(summary)}


@app.get("/api/datasets")
def list_builtin_datasets(problem_type: str | None = None) -> dict[str, Any]:
    """Return real parser-derived summaries for datasets currently present on disk."""
    selected_type = problem_type.lower() if problem_type else None
    if selected_type is not None and selected_type not in DATASET_PROBLEM_TYPES:
        raise HTTPException(422, "Problem type must be LP, MILP, or QP.")
    datasets: list[dict[str, Any]] = []
    for entry in _dataset_entries(selected_type):
        item = {key: value for key, value in entry.items() if key != "path"}
        try:
            source_stat = entry["path"].stat()
            cache_key = (str(entry["path"].resolve()), source_stat.st_mtime_ns, source_stat.st_size)
            with DATASET_CACHE_LOCK:
                cached = DATASET_ANALYSIS_CACHE.get(cache_key)
            if cached is None:
                with tempfile.TemporaryDirectory(prefix="sovereign-dataset-stats-") as temporary:
                    _, analysis = _prepare_model(entry["path"], Path(temporary))
                cached = {"analysis": _public_analysis(analysis), "available": True}
                with DATASET_CACHE_LOCK:
                    DATASET_ANALYSIS_CACHE[cache_key] = cached
            item.update(cached)
        except (OSError, ValueError, UnicodeError) as exc:
            item["analysis"] = None
            item["available"] = False
            item["error"] = str(exc)
        datasets.append(item)
    return {"datasets": datasets, "problem_types": [kind.upper() for kind in DATASET_PROBLEM_TYPES]}


@app.post("/api/datasets/{problem_type}/{size}/{dataset_name}/analyze")
def analyze_builtin_dataset(problem_type: str, size: str, dataset_name: str) -> dict[str, Any]:
    dataset_id = f"{problem_type.lower()}/{size.lower()}/{dataset_name.lower()}"
    entry = next((item for item in _dataset_entries(problem_type.lower()) if item["id"].lower() == dataset_id), None)
    if entry is None:
        raise HTTPException(404, "Built-in dataset was not found.")
    directory = Path(tempfile.mkdtemp(prefix="sovereign-dataset-"))
    destination = directory / _safe_name(entry["file_name"])
    try:
        shutil.copy2(entry["path"], destination)
        job_id, summary = _analyze_job_file(destination, directory)
    except UnsupportedQPLIBFeature as exc:
        shutil.rmtree(directory, ignore_errors=True)
        raise HTTPException(422, f"Built-in model unsupported: {exc}") from exc
    except (OSError, ValueError, UnicodeError) as exc:
        shutil.rmtree(directory, ignore_errors=True)
        raise HTTPException(422, f"Built-in model parsing failed: {exc}") from exc
    return {"job_id": job_id, "analysis": _public_analysis(summary)}


@app.post("/api/examples/{example_name}")
def load_example(example_name: str) -> dict[str, Any]:
    examples = {
        "lp": (ROOT / "examples" / "lp.json"),
        "qp": (ROOT / "examples" / "convex_qp.json"),
        "milp": (ROOT / "examples" / "milp_relaxation.json"),
        "netlib-emps": (ROOT / "examples" / "afiro.emps.txt"),
    }
    source = examples.get(example_name)
    if source is None or not source.is_file():
        raise HTTPException(404, "Example was not found.")
    job_id = uuid.uuid4().hex
    directory = Path(tempfile.mkdtemp(prefix=f"sovereign-{job_id[:8]}-"))
    destination = directory / source.name
    shutil.copy2(source, destination)
    try:
        job_id, summary = _analyze_job_file(destination, directory)
    except ValueError as exc:
        shutil.rmtree(directory, ignore_errors=True)
        raise HTTPException(422, f"Model parsing failed: {exc}") from exc
    return {"job_id": job_id, "analysis": _public_analysis(summary)}


def _run_solver(job_id: str, job: dict[str, Any], configuration: dict[str, Any]) -> dict[str, Any]:
    global ACTIVE_SOLVER_PROCESS, ACTIVE_SOLVER_JOB_ID
    if job.get("state") == "cancelled":
        raise HTTPException(409, "This model's previous run was cancelled and its temporary input was removed. Upload or select the model again before starting a new solve.")
    started = time.perf_counter()
    info = device_info()
    model_size = SolverPolicy.limits_for(job["analysis"])[0].name
    configuration["model_size"] = model_size
    configuration["problem_type"] = str(job["analysis"].get("problem_type", "UNKNOWN")).upper()
    presolve_work = sum(max(0, int(job["analysis"].get(key, 0))) for key in ("variables", "constraints", "nonzeros"))
    requested_time_limit = int(configuration.get("time_limit_seconds", 0))
    if (configuration.get("problem_type") == "LP" and configuration.get("presolve", True)
            and presolve_work >= SolverPolicy.ADAPTIVE_PRESOLVE_WORK_THRESHOLD and requested_time_limit > 0):
        configuration["presolve_time_limit_ms"] = min(2000.0, max(25.0, requested_time_limit * 50.0))
    else:
        configuration["presolve_time_limit_ms"] = 0.0
    # Auto always selects an in-house Sovereign algorithm. Reference solvers
    # are used only by the separate, explicitly requested comparison route.
    if configuration.get("method") == "auto":
        selection = SolverPolicy.select(job["analysis"], info)
        configuration["method"] = selection.method
        configuration["selected_algorithm"] = selection.method
        configuration["user_selected_algorithm"] = selection.method
        configuration["max_iterations"] = selection.max_iterations
        if int(configuration.get("time_limit_seconds", 0)) == 0:
            configuration["time_limit_seconds"] = selection.time_limit_seconds
        configuration["execution_time_limit_seconds"] = configuration["time_limit_seconds"]
    requested_backend = configuration.get("backend", "auto")
    if requested_backend == "cuda" and configuration.get("method") not in {"ipm", "qp"}:
        raise HTTPException(422, f"CUDA execution is not implemented for {configuration.get('method')}.")
    configuration["backend_requested"] = requested_backend
    if requested_backend == "cuda" and not info["cuda_available"]:
        raise HTTPException(409, "CUDA backend is unavailable on this system.")
    try:
        if requested_backend == "cuda":
            solver = _cuda_solver_path()
        elif requested_backend == "cpu":
            solver = _cpu_solver_path()
        elif configuration["method"] in {"ipm", "qp"} and info["cuda_available"]:
            try:
                solver = _cuda_solver_path()
            except FileNotFoundError:
                # The CPU executable can still honor the same C++ auto policy,
                # which will fall back to CPU when its CUDA context is absent.
                solver = _cpu_solver_path()
        else:
            solver = _cpu_solver_path()
    except FileNotFoundError as exc:
        raise HTTPException(503, str(exc)) from exc
    command = [str(solver), "--input", str(job["path"]), "--backend", requested_backend]
    command.extend(["--model-size", model_size])
    if configuration["method"] != "auto":
        command.extend(["--method", configuration["method"]])
    if not configuration["presolve"]:
        command.append("--no-presolve")
    if configuration.get("presolve_time_limit_ms", 0.0) > 0:
        command.extend(["--presolve-time-ms", str(configuration["presolve_time_limit_ms"])])
    if configuration["method"] not in {"cutting-plane", "feasibility-pump", "lp-relaxation"}:
        command.extend(["--max-iterations", str(configuration["max_iterations"])])
    if configuration["method"] == "milp":
        command.extend(["--max-nodes", str(configuration.get("max_nodes", 10000))])
        if int(job["analysis"].get("variables", 0)) >= 50_000:
            command.append("--sparse-primal")
    execution_limit = float(configuration.get("execution_time_limit_seconds", configuration["time_limit_seconds"]))
    process: subprocess.Popen[str] | None = None
    try:
        with JOB_LOCK:
            if ACTIVE_SOLVER_PROCESS is not None and ACTIVE_SOLVER_PROCESS.poll() is None:
                active = ACTIVE_SOLVER_JOB_ID or "another job"
                raise HTTPException(409, f"Optimization is already running for {active}. Cancel it before starting another run.")
            job["state"] = "solving"
            job["configuration"] = configuration
            job["started_at"] = time.time()
            process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
            job["process"] = process
            ACTIVE_SOLVER_PROCESS = process
            ACTIVE_SOLVER_JOB_ID = job_id
        if execution_limit > 0:
            stdout, stderr = process.communicate(timeout=execution_limit)
        else:
            stdout, stderr = process.communicate()
        total_ms = (time.perf_counter() - started) * 1000
        raw_output = (stdout or "") + (("\nSTDERR:\n" + stderr) if stderr else "")
        result = parse_solver_output(raw_output, total_ms)
        actual_backend = str(result["metrics"].get("backend") or "").lower()
        if actual_backend in {"cpu", "cuda"}:
            configuration["backend"] = actual_backend
        else:
            configuration["backend"] = "unknown"
        configuration["backend_reason"] = result["metrics"].get("backend_reason")
        _record_execution_trace(result, job, configuration)
        result["timings"]["model_preparation_time_ms"] = float(job.get("preparation_time_ms", 0.0))
        _publish_timing_contract(result, total_ms + float(job.get("preparation_time_ms", 0.0)))
        if process.returncode != 0 and result["status"] == "FAILED":
            detail = next((line.strip() for line in reversed((stderr or "").splitlines()) if line.strip()), "")
            if "bad allocation" in detail.lower() or "out of memory" in detail.lower():
                result["status"] = "MEMORY_LIMIT"
                result["metrics"]["message"] = "The C++ solver ran out of memory while building or solving the LP relaxation."
            else:
                result["status"] = "SOLVER_ERROR"
                result["metrics"]["message"] = f"Solver process exited with code {process.returncode}. {detail}".strip()
    except subprocess.TimeoutExpired as exc:
        process.kill()
        stdout, stderr = process.communicate()
        total_ms = (time.perf_counter() - started) * 1000
        text = (stdout or exc.stdout or "") if isinstance(stdout or exc.stdout, str) else ""
        result = parse_solver_output(text, total_ms)
        actual_backend = str(result["metrics"].get("backend") or "").lower()
        configuration["backend"] = actual_backend if actual_backend in {"cpu", "cuda"} else "unknown"
        configuration["backend_reason"] = result["metrics"].get("backend_reason")
        _record_execution_trace(result, job, configuration)
        result["timings"]["model_preparation_time_ms"] = float(job.get("preparation_time_ms", 0.0))
        _publish_timing_contract(result, total_ms + float(job.get("preparation_time_ms", 0.0)))
        result["status"] = "TIME_LIMIT"
        result["metrics"]["message"] = f"The solver reached the configured {execution_limit:g}-second time limit."
    finally:
        with JOB_LOCK:
            job.pop("process", None)
            if process is not None and ACTIVE_SOLVER_PROCESS is process:
                ACTIVE_SOLVER_PROCESS = None
                ACTIVE_SOLVER_JOB_ID = None
    if job.pop("cancel_requested", False):
        result["status"] = "CANCELLED"
        result["verification"] = "N/A"
        result["metrics"]["message"] = "Optimization was cancelled by the user."
    return result


def _complete_result(job_id: str, job: dict[str, Any], configuration: dict[str, Any], result: dict[str, Any], automation: dict[str, Any]) -> dict[str, Any]:
    result.update({
        "job_id": job_id,
        "analysis": _public_analysis(job["analysis"]),
        "configuration": configuration,
        "automation": automation,
        "pipeline": ["parse", "validate", "presolve", "solve", "postsolve", "verify"],
    })
    result["variables"], result["variable_summary"] = _solution_variables(result["variables"], job["analysis"]["variable_metadata"], result.pop("sparse_primal", False))
    job["state"], job["result"] = ("cancelled" if result["status"] == "CANCELLED" else "complete"), result
    if result["status"] == "CANCELLED":
        shutil.rmtree(job["directory"], ignore_errors=True)
    return result


@app.post("/api/solve")
def solve(request: SolveRequest) -> dict[str, Any]:
    job = JOBS.get(request.job_id)
    if not job:
        raise HTTPException(404, "Unknown or expired job ID. Analyze a model first.")
    request_started = time.perf_counter()
    configuration = request.model_dump()
    if request.selected_algorithm:
        configuration["method"] = request.selected_algorithm
    configuration["selected_algorithm"] = configuration["method"]
    detected_type = str(job["analysis"].get("problem_type", "")).upper()
    if configuration.get("time_limit_seconds") is None:
        configuration["time_limit_seconds"] = SolverPolicy.limits_for(job["analysis"])[0].time_limit_seconds
    if request.problem_type and request.problem_type != detected_type:
        raise HTTPException(409, f"The submitted problem type {request.problem_type} does not match the analyzed model type {detected_type}.")
    if configuration["method"] != "auto":
        compatible = {
            "LP": {"revised-simplex", "dual-simplex", "ipm"},
            "MILP": {"milp", "cutting-plane", "feasibility-pump", "lp-relaxation"},
            "QP": {"qp"},
        }
        if configuration["method"] not in compatible.get(detected_type, set()):
            raise HTTPException(422, f"{configuration['method']} is not a supported method for {detected_type} models.")
    effective_method = configuration["method"]
    if effective_method == "auto":
        effective_method = SolverPolicy.select(job["analysis"], device_info()).method
    if configuration["backend"] == "cuda" and effective_method not in {"ipm", "qp"}:
        raise HTTPException(422, f"CUDA execution is not implemented for {effective_method}.")
    configuration["execution_time_limit_seconds"] = configuration["time_limit_seconds"]
    result = _run_solver(request.job_id, job, configuration)
    completed = _complete_result(request.job_id, job, configuration, result, {"mode": "expert", "attempts": [{"method": configuration["method"], "backend": configuration["backend"], "status": result["status"], "verification": result["verification"]}]})
    _publish_timing_contract(completed, float(job.get("preparation_time_ms", 0.0)) + (time.perf_counter() - request_started) * 1000)
    job["result"] = completed
    return completed


@app.post("/api/route/{job_id}")
def preview_solver_route(job_id: str, request: RoutePreviewRequest) -> dict[str, Any]:
    """Ask the same C++ size-aware selector used at solve time to preview CPU/CUDA."""
    job = JOBS.get(job_id)
    if not job:
        raise HTTPException(404, "Unknown or expired job ID. Analyze a model first.")
    detected_type = str(job["analysis"].get("problem_type", "")).upper()
    if request.problem_type != detected_type:
        raise HTTPException(409, f"The submitted problem type {request.problem_type} does not match the analyzed model type {detected_type}.")
    compatible = {
        "LP": {"revised-simplex", "dual-simplex", "ipm"},
        "MILP": {"milp", "cutting-plane", "feasibility-pump"},
        "QP": {"qp"},
    }
    if request.selected_algorithm not in compatible.get(detected_type, set()):
        raise HTTPException(422, f"{request.selected_algorithm} is not a supported method for {detected_type} models.")
    info = device_info()
    solver: Path
    if request.selected_algorithm in {"ipm", "qp"} and info["cuda_available"]:
        try:
            solver = _cuda_solver_path()
        except FileNotFoundError:
            solver = _cpu_solver_path()
    else:
        solver = _cpu_solver_path()
    model_size = SolverPolicy.limits_for(job["analysis"])[0].name
    analysis = job["analysis"]
    command = [str(solver), "--select-backend", "--backend", "auto",
               "--method", request.selected_algorithm, "--model-size", model_size,
               "--rows", str(int(analysis.get("constraints", 0))),
               "--cols", str(int(analysis.get("variables", 0))),
               "--nnz", str(int(analysis.get("nonzeros", 0)))]
    try:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=30, check=False)
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(504, "The C++ backend selector did not finish model analysis in time.") from exc
    except OSError as exc:
        raise HTTPException(503, f"Could not start the C++ backend selector: {exc}") from exc
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "Backend selection failed.").strip()
        raise HTTPException(422, detail[-1200:])
    output = completed.stdout
    backend = _capture(r"^Backend:\s*(cpu|cuda)$", output, None, str)
    reason = _capture(r"^Backend reason:\s*(.+)$", output, None, str)
    if backend not in {"cpu", "cuda"}:
        raise HTTPException(502, "The C++ backend selector did not return an execution backend.")
    return {
        "job_id": job_id,
        "problem_type": detected_type,
        "selected_algorithm": request.selected_algorithm,
        "backend_requested": "auto",
        "backend": backend,
        "reason": reason,
        "variables": _capture(r"^Variables:\s*(\d+)$", output, 0, int),
        "constraints": _capture(r"^Constraints:\s*(\d+)$", output, 0, int),
        "nonzeros": _capture(r"^Nonzeros:\s*(\d+)$", output, 0, int),
        "cuda_available": bool(info["cuda_available"]),
    }


@app.get("/api/policy/{job_id}")
def automatic_policy(job_id: str) -> dict[str, Any]:
    job = JOBS.get(job_id)
    if not job:
        raise HTTPException(404, "Unknown or expired job ID. Analyze a model first.")
    return {"job_id": job_id, "selection": SolverPolicy.select(job["analysis"], device_info()).payload()}


@app.post("/api/solve/auto")
def solve_automatically(request: AutoSolveRequest) -> dict[str, Any]:
    job = JOBS.get(request.job_id)
    if not job:
        raise HTTPException(404, "Unknown or expired job ID. Analyze a model first.")
    request_started = time.perf_counter()
    selection = SolverPolicy.select(job["analysis"], device_info())
    configuration = {"job_id": request.job_id, **selection.payload()}
    configuration.pop("fallback_methods")
    configuration["selected_algorithm"] = selection.method
    configuration["backend"] = "auto"
    configuration["execution_time_limit_seconds"] = configuration["time_limit_seconds"]
    result = _run_solver(request.job_id, job, configuration)
    def attempt_record(method: str, backend: str, attempt_result: dict[str, Any], reason: str) -> dict[str, Any]:
        timings = attempt_result.get("timings") or {}
        metrics = attempt_result.get("metrics") or {}
        return {
            "method": method,
            "backend": backend,
            "status": attempt_result["status"],
            "verification": attempt_result["verification"],
            "reason": reason,
            "time_ms": timings.get("backend_total_time_ms"),
            "presolve_time_ms": timings.get("presolve_time_ms"),
            "solver_time_ms": timings.get("solver_time_ms"),
            "postsolve_time_ms": timings.get("postsolve_time_ms"),
            "verification_time_ms": timings.get("verification_time_ms"),
            "iterations": metrics.get("iterations"),
            "message": metrics.get("message"),
            "estimated_dense_memory_bytes": metrics.get("estimated_dense_memory_bytes"),
            "dense_memory_guard": metrics.get("dense_memory_guard_triggered"),
        }

    attempts = [attempt_record(configuration["method"], configuration["backend"], result, "Initial automatic selection")]
    fallback_reason: str | None = None
    eligible_for_fallback = bool(selection.fallback_methods) and SolverPolicy.can_fallback(result["status"], result["verification"])
    if eligible_for_fallback and not SolverPolicy.dense_fallback_is_safe(job["analysis"]):
        estimated = SolverPolicy.dense_fallback_memory_bytes(job["analysis"])
        fallback_reason = f"Skipped dense Dual Simplex fallback: estimated workspace {estimated:,} bytes exceeds the {SolverPolicy.DENSE_FALLBACK_MEMORY_BUDGET_BYTES:,}-byte safety budget."
    elif eligible_for_fallback:
        limit_seconds = int(configuration.get("time_limit_seconds", 0))
        elapsed_seconds = time.perf_counter() - request_started
        remaining_seconds = limit_seconds - elapsed_seconds if limit_seconds > 0 else 0
        if limit_seconds > 0 and remaining_seconds <= 0:
            fallback_reason = "Skipped fallback because the automatic request's shared time limit is exhausted."
    if eligible_for_fallback and fallback_reason is None:
        fallback_method = selection.fallback_methods[0]
        fallback_backend = "auto"
        fallback_configuration = {**configuration, "method": fallback_method, "backend": fallback_backend}
        limit_seconds = int(configuration.get("time_limit_seconds", 0))
        fallback_configuration["execution_time_limit_seconds"] = max(0.01, limit_seconds - (time.perf_counter() - request_started)) if limit_seconds > 0 else 0
        fallback = _run_solver(request.job_id, job, fallback_configuration)
        attempts.append(attempt_record(fallback_method, fallback_configuration["backend"], fallback, f"Fallback after {result['status']} / verification {result['verification']}"))
        result, configuration = fallback, fallback_configuration
    elif eligible_for_fallback and fallback_reason:
        attempts.append({"method": selection.fallback_methods[0], "backend": "auto", "status": "SKIPPED", "verification": "N/A", "reason": fallback_reason, "time_ms": 0.0, "iterations": 0})
    automation = {"mode": "automatic", "selection": selection.payload(), "attempts": attempts, "final_method": configuration["method"], "final_backend": configuration["backend"], "fallback_reason": fallback_reason}
    completed = _complete_result(request.job_id, job, configuration, result, automation)
    _publish_timing_contract(completed, float(job.get("preparation_time_ms", 0.0)) + (time.perf_counter() - request_started) * 1000)
    job["result"] = completed
    return completed


@app.post("/api/solve/{job_id}/cancel")
def cancel_solve(job_id: str) -> dict[str, Any]:
    job = JOBS.get(job_id)
    if not job:
        raise HTTPException(404, "Unknown job ID.")
    with JOB_LOCK:
        process = job.get("process")
        if process is None or process.poll() is not None:
            raise HTTPException(409, "No active solver process is running for this job.")
        job["cancel_requested"] = True
        process.terminate()
    return {"job_id": job_id, "state": "cancelling", "message": "Cancellation requested for the active solver process."}


@app.get("/api/solve/{job_id}")
@app.get("/api/results/{job_id}")
def get_result(job_id: str) -> dict[str, Any]:
    job = JOBS.get(job_id)
    if not job:
        raise HTTPException(404, "Unknown job ID.")
    return {"job_id": job_id, "state": job["state"], "analysis": _public_analysis(job["analysis"]), "configuration": job.get("configuration"), "started_at": job.get("started_at"), "result": job.get("result")}


@app.get("/api/results/{job_id}/export")
def export_result(job_id: str, format: str = "csv") -> Response:
    job = JOBS.get(job_id)
    result = job.get("result") if job else None
    if result is None:
        raise HTTPException(404, "A completed result was not found for this job.")
    rows = result.get("variables", [])
    if format == "json":
        summary = result.get("variable_summary") or {}
        payload = json.dumps({"job_id": job_id, "variable_summary": summary, "sparse_solution": bool(summary.get("sparse")), "zero_values_omitted": bool(summary.get("sparse")), "variables": rows}, indent=2)
        return Response(payload, media_type="application/json", headers={"Content-Disposition": f'attachment; filename="sovereign_solution_{job_id}.json"'})
    if format != "csv":
        raise HTTPException(422, "Export format must be csv or json.")
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=["name", "value", "type", "lower", "upper", "at_bound", "fractional"])
    writer.writeheader()
    if (result.get("variable_summary") or {}).get("sparse"):
        nonzero = {row["name"]: row for row in rows}
        def csv_chunks():
            buffer = io.StringIO(newline="")
            chunk_writer = csv.DictWriter(buffer, fieldnames=["name", "value", "type", "lower", "upper", "at_bound", "fractional"])
            chunk_writer.writeheader()
            yield buffer.getvalue()
            for meta in job["analysis"]["variable_metadata"]:
                if isinstance(meta, tuple):
                    name, variable_type, lower, upper = meta
                else:
                    name, variable_type, lower, upper = meta["name"], meta.get("type"), meta.get("lower"), meta.get("upper")
                row = nonzero.get(name)
                value = row["value"] if row else 0.0
                chunk_writer.writerow({"name":name,"value":value,"type":variable_type,"lower":lower,"upper":upper,"at_bound":row["at_bound"] if row else (lower == 0 or upper == 0),"fractional":row["fractional"] if row else False})
                if buffer.tell() >= 64 * 1024:
                    yield buffer.getvalue()
                    buffer.seek(0); buffer.truncate(0)
            if buffer.tell():
                yield buffer.getvalue()
        return StreamingResponse(csv_chunks(), media_type="text/csv", headers={"Content-Disposition": f'attachment; filename="sovereign_solution_{job_id}.csv"'})
    writer.writerows(rows)
    return Response(output.getvalue(), media_type="text/csv", headers={"Content-Disposition": f'attachment; filename="sovereign_solution_{job_id}.csv"'})


@app.get("/api/benchmarks")
def benchmarks() -> dict[str, Any]:
    datasets: list[dict[str, Any]] = []
    for suite_name in ("netlib", "mittelmann", "miplib", "qplib"):
        directory = ROOT / "results" / suite_name
        report = directory / f"{directory.name}_benchmark.json"
        if not report.is_file():
            datasets.append({"dataset": suite_name, "metadata": {}, "instances": []})
            continue
        try:
            data = json.loads(report.read_text(encoding="utf-8"))
            instances = data.get("instances", [])
            external_path = directory / "external_solver_comparison.json"
            external = {}
            if external_path.is_file():
                try:
                    external = {Path(row.get("instance") or "").name.lower(): row
                                for row in json.loads(external_path.read_text(encoding="utf-8"))
                                if row.get("instance")}
                except (OSError, json.JSONDecodeError, TypeError):
                    external = {}
            datasets.append({
                "dataset": directory.name,
                "metadata": data.get("metadata", {}),
                "instances": [{
                    "instance": row.get("instance"), "status": row.get("status"),
                    "objective": row.get("objective_value", row.get("objective")), "verification_pass": row.get("verification_pass"),
                    "iterations": row.get("iterations"), "nodes_processed": row.get("nodes_processed"),
                    "solve_time_ms": row.get("solve_time_ms"), "failure_reason": row.get("failure_reason"),
                    "timing": row.get("timing") or {
                        "parsing_ms": row.get("parse_time_ms"), "presolve_ms": row.get("presolve_time_ms"),
                        "solver_ms": row.get("solve_time_ms"), "postsolve_ms": row.get("postsolve_time_ms"),
                        "verification_ms": row.get("verification_time_ms"), "backend_total_ms": row.get("total_time_ms"),
                    },
                    "problem_type": row.get("problem_class"), "variables": row.get("number_of_variables"),
                    "constraints": row.get("number_of_constraints"), "nonzeros": row.get("number_of_nonzeros"),
                    "sparsity": ((1.0 - row.get("number_of_nonzeros") / (row.get("number_of_variables") * row.get("number_of_constraints"))) * 100
                                 if row.get("number_of_nonzeros") is not None and row.get("number_of_variables") and row.get("number_of_constraints") else None),
                    "algorithm": row.get("solver_method"),
                    "backend": row.get("backend"), "best_bound": row.get("best_dual_bound"),
                    "relative_gap": row.get("relative_gap"), "parse_time_ms": row.get("parse_time_ms"),
                    "presolve_time_ms": row.get("presolve_time_ms"), "postsolve_time_ms": row.get("postsolve_time_ms"),
                    "verification_time_ms": row.get("verification_time_ms"), "total_time_ms": row.get("total_time_ms"),
                    "primal_residual": row.get("primal_residual"), "dual_residual": row.get("dual_residual"),
                    "complementarity_residual": row.get("complementarity_residual"), "convexity": row.get("convexity"),
                    "hessian_type": row.get("hessian_type"), "nodes_created": row.get("nodes_created"),
                    "cuts_generated": row.get("cuts_generated"), "feasibility_pump_used": row.get("feasibility_pump_used"),
                    "highs": external.get(Path(row.get("instance") or "").name.lower()),
                } for row in instances],
            })
        except (OSError, json.JSONDecodeError):
            datasets.append({"dataset": suite_name, "metadata": {}, "instances": [], "error": "Benchmark report could not be read."})
    return {"datasets": datasets, "comparators": comparator_availability()}


@app.delete("/api/benchmarks/{dataset}")
def delete_benchmark_records(dataset: str) -> dict[str, Any]:
    """Delete generated result reports for one known suite, never source models."""
    suite = dataset.lower()
    if suite not in {"netlib", "mittelmann", "miplib", "qplib"}:
        raise HTTPException(404, "Unknown benchmark suite.")
    directory = ROOT / "results" / suite
    report_names = (
        f"{suite}_benchmark.json", "lp_benchmark.csv", "lp_benchmark.json",
        "milp_benchmark.csv", "milp_benchmark.json", "qp_benchmark.csv", "qp_benchmark.json",
        "external_solver_comparison.csv", "external_solver_comparison.json",
        "cpu_gpu_benchmark.csv", "cpu_gpu_benchmark.json",
        "presolve_benchmark.csv", "presolve_benchmark.json",
        "numerical_robustness.csv", "numerical_robustness.json", "benchmark_summary.md",
    )
    deleted = []
    for name in report_names:
        path = directory / name
        if path.is_file() and path.parent == directory:
            path.unlink()
            deleted.append(name)
    return {"dataset": suite, "deleted_files": deleted, "deleted_count": len(deleted)}


def _compare_prepared_model(
    source: Path,
    solver_path: Path,
    analysis: dict[str, Any],
    preparation_time_ms: float,
    time_limit_seconds: int,
    directory: Path,
    active_model_id: str | None = None,
    selected_configuration: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Run the standard comparison pipeline on an already analyzed source model."""
    model = parse_problem_file(str(solver_path))
    job_id = uuid.uuid4().hex
    selection = SolverPolicy.select(analysis, device_info())
    configuration = {"job_id": job_id, **selection.payload()}
    configuration.pop("fallback_methods", None)
    # Compare This Model reruns the same selected Sovereign algorithm and
    # resolved backend that produced the active result. Standalone uploaded
    # comparisons use the conservative default method with backend AUTO.
    if selected_configuration:
        configuration["method"] = selected_configuration.get("user_selected_algorithm") or selected_configuration.get("method") or selection.method
        configuration["selected_algorithm"] = configuration["method"]
        requested_backend = selected_configuration.get("resolved_backend") or selected_configuration.get("backend")
        configuration["backend"] = requested_backend if requested_backend in {"cpu", "cuda"} else "auto"
        configuration["presolve"] = bool(selected_configuration.get("presolve", True))
        configuration["max_iterations"] = int(selected_configuration.get("max_iterations", selection.max_iterations))
        configuration["max_nodes"] = int(selected_configuration.get("max_nodes", selection.max_nodes))
    else:
        configuration["backend"] = "auto"
    configuration["execution_time_limit_seconds"] = time_limit_seconds
    configuration["time_limit_seconds"] = time_limit_seconds
    active_job = JOBS.get(active_model_id) if active_model_id else None
    cached_result = active_job.get("result") if active_job and active_job.get("state") == "complete" else None
    reuse_active_result = bool(
        cached_result
        and cached_result.get("verification") == "PASS"
        and str(cached_result.get("status", "")).upper() in {"OPTIMAL", "FEASIBLE"}
    )
    job = {"directory": directory, "path": solver_path, "source_path": source,
           "analysis": analysis, "preparation_time_ms": preparation_time_ms, "state": "analyzed"}
    if not reuse_active_result:
        JOBS[job_id] = job
    try:
        if reuse_active_result:
            sovereign_result = cached_result
            configuration = sovereign_result.get("configuration") or configuration
        else:
            solve_started = time.perf_counter()
            sovereign_result = _run_solver(job_id, job, configuration)
            _publish_timing_contract(sovereign_result, preparation_time_ms + (time.perf_counter() - solve_started) * 1000)
        metrics = sovereign_result.get("metrics") or {}
        actual_backend = str(configuration.get("backend") or "unknown").lower()
        delegated = actual_backend not in {"cpu", "cuda", "unknown"}
        sovereign = {
            "solver": "website-auto" if delegated else "sovereign",
            "display_name": (f"Auto → {actual_backend.upper()}"
                             f"{' Barrier' if metrics.get('resolved_algorithm') == 'gurobi-barrier' else ''}")
                            if delegated else "Sovereign Native",
            "status": sovereign_result.get("status", "FAILED"),
            "objective": metrics.get("objective"),
            "solve_time_ms": (sovereign_result.get("timings") or {}).get("solver_time_ms"),
            "timing": sovereign_result.get("timing"),
            "iterations": metrics.get("iterations"), "nodes": metrics.get("nodes_processed"),
            "best_bound": metrics.get("dual_bound"), "relative_gap": metrics.get("relative_gap"),
            "verification": sovereign_result.get("verification", "N/A"),
            "backend": actual_backend, "algorithm": metrics.get("resolved_algorithm") or configuration.get("method"),
            "convexity": metrics.get("convexity"), "primal_residual": metrics.get("primal_residual"),
            "dual_residual": metrics.get("dual_residual"),
            "complementarity_residual": metrics.get("complementarity_residual"),
            "failure_reason": metrics.get("message"),
        }
        references = [solve_comparator(name, model, float(time_limit_seconds)) for name in ("highs", "gurobi", "cplex")]
        for reference in references:
            if reference.get("solver") in {"highs", "gurobi", "cplex"}:
                reference["display_name"] = f"{reference['solver'].upper()} · Default settings"
        solvers = [sovereign, *references]
        objective_tolerance = 1e-6
        available_objectives = [row for row in solvers if row.get("verification") == "PASS"
                                and isinstance(row.get("objective"), (int, float))]
        agreement = None
        comparisons = []
        if len(available_objectives) > 1:
            base = sovereign.get("objective")
            for reference in available_objectives:
                if reference is sovereign or not isinstance(base, (int, float)):
                    continue
                difference = abs(float(base) - float(reference["objective"]))
                relative = difference / max(1.0, abs(float(base)), abs(float(reference["objective"])))
                matches = difference <= objective_tolerance or relative <= objective_tolerance
                comparisons.append({"solver": reference["solver"], "absolute_difference": difference,
                                    "relative_difference": relative, "agree": matches})
            agreement = all(item["agree"] for item in comparisons) if comparisons else None
        env = device_info()
        try:
            from sovereign_solver.benchmark import _total_memory_bytes
            ram_bytes = _total_memory_bytes()
        except Exception:
            ram_bytes = None
        return {
            "instance": source.name, "active_model_id": active_model_id,
            "reused_active_solve": reuse_active_result,
            "problem_type": analysis.get("problem_type"),
            "model": {key: analysis.get(key) for key in ("variables", "constraints", "nonzeros", "sparsity", "objective_sense")},
            "time_limit_seconds": time_limit_seconds, "hardware": {
                "cpu": platform.processor() or None, "gpu": env.get("gpu_name"),
                "cuda_available": env.get("cuda_available"), "cuda_version": env.get("cuda_runtime"),
                "ram_bytes": ram_bytes, "os": platform.platform(),
            },
            "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "fairness": ("The Auto timing is reused from the completed solve; reference solvers were independently timed on the same input and machine with the selected time limit. Solver settings and algorithms remain solver-specific."
                         if reuse_active_result else
                         "Same input model, same machine, same wall-clock time limit; solver settings and algorithms remain solver-specific."),
            "solver_results": solvers, "objective_agreement": agreement, "objective_comparisons": comparisons,
        }
    finally:
        if not reuse_active_result:
            JOBS.pop(job_id, None)


@app.post("/api/benchmarks/compare")
async def compare_benchmark_model(
    file: UploadFile = File(...),
    time_limit_seconds: int = Form(default=60, ge=1, le=3600),
) -> dict[str, Any]:
    suffix = Path(file.filename or "").suffix.lower()
    if suffix not in ALLOWED_EXTENSIONS:
        raise HTTPException(415, "Supported benchmark formats are MPS, JSON, TXT, and QPLIB.")
    payload = await file.read(MAX_UPLOAD_BYTES + 1)
    if len(payload) > MAX_UPLOAD_BYTES:
        raise HTTPException(413, f"File exceeds the {MAX_UPLOAD_BYTES // (1024 * 1024)} MB upload limit.")
    directory = Path(tempfile.mkdtemp(prefix="sovereign-compare-"))
    source = directory / _safe_name(file.filename or f"benchmark{suffix}")
    try:
        source.write_bytes(payload)
        preparation_started = time.perf_counter()
        solver_path, analysis = _prepare_model(source, directory)
        preparation_time_ms = (time.perf_counter() - preparation_started) * 1000
        return _compare_prepared_model(source, solver_path, analysis, preparation_time_ms,
                                       time_limit_seconds, directory)
    except (ValueError, OSError, UnicodeError) as exc:
        raise HTTPException(422, f"Benchmark model could not be prepared: {exc}") from exc
    finally:
        shutil.rmtree(directory, ignore_errors=True)


@app.post("/api/benchmarks/compare-active")
def compare_active_model(request: ActiveModelCompareRequest) -> dict[str, Any]:
    active = JOBS.get(request.model_id)
    if active is None:
        raise HTTPException(404, "The active model is no longer available. Select or upload it again.")
    if active.get("state") == "cancelled":
        raise HTTPException(409, "The active model was cancelled and is no longer available. Select it again.")
    if active.get("state") == "solving":
        raise HTTPException(409, "Wait for the current solve to finish before comparing solvers.")
    original_source = Path(active.get("source_path", ""))
    if not original_source.is_file():
        raise HTTPException(410, "The active model file is no longer available. Select it again.")
    directory = Path(tempfile.mkdtemp(prefix="sovereign-active-compare-"))
    source = directory / _safe_name(original_source.name)
    try:
        shutil.copyfile(original_source, source)
        preparation_started = time.perf_counter()
        solver_path, analysis = _prepare_model(source, directory)
        preparation_time_ms = (time.perf_counter() - preparation_started) * 1000
        active_configuration = active.get("configuration") or (active.get("result") or {}).get("configuration")
        return _compare_prepared_model(source, solver_path, analysis, preparation_time_ms,
                                       request.time_limit_seconds, directory, request.model_id,
                                       active_configuration)
    except (ValueError, OSError, UnicodeError) as exc:
        raise HTTPException(422, f"Active benchmark model could not be prepared: {exc}") from exc
    finally:
        shutil.rmtree(directory, ignore_errors=True)


@app.get("/")
def index() -> FileResponse:
    return FileResponse(STATIC / "index.html")


app.mount("/", StaticFiles(directory=STATIC, html=True), name="static")
