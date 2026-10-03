"""Repeatable local LP timing harness for before/after solver builds."""
from __future__ import annotations

import argparse
import json
import platform
import re
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INSTANCES = {
    "afiro": ROOT / "benchmarks/netlib/small/afiro.mps",
    "afiro2": ROOT / "benchmarks/netlib/small/afiro2.mps",
    "adlittle": ROOT / "benchmarks/netlib/small/adlittle.mps",
    "agg": ROOT / "benchmarks/netlib/small/agg.mps",
    "bandm": ROOT / "benchmarks/netlib/small/bandm.mps",
    "boeing1": ROOT / "benchmarks/netlib/small/boeing1.mps",
    "boeing2": ROOT / "benchmarks/netlib/small/boeing2.mps",
    "scagr25": ROOT / "benchmarks/netlib/small/scagr25.mps",
    "scagr7": ROOT / "benchmarks/netlib/small/scagr7.mps",
    "pilot": ROOT / "benchmarks/netlib/small/pilot.mps",
    "small": ROOT / "benchmarks/netlib/small/small.mps",
    "stocfor1": ROOT / "benchmarks/netlib/small/stocfor1.mps",
    "stocfor2": ROOT / "benchmarks/netlib/small/stocfor2.mps",
}
TIMING_FIELDS = {
    "parse_ms": "Parse time ms",
    "presolve_ms": "Presolve time ms",
    "standardization_ms": "Standardization time ms",
    "solve_ms": "Solve time ms",
    "solve_pipeline_ms": "Solve pipeline time ms",
    "sparse_pricing_ms": "Sparse pricing time ms",
    "sparse_basis_solve_ms": "Sparse basis solve time ms",
    "sparse_devex_ms": "Sparse Devex time ms",
    "sparse_factorization_ms": "Sparse factorization time ms",
    "sparse_ratio_test_ms": "Sparse ratio test time ms",
    "sparse_lexicographic_ms": "Sparse lexicographic time ms",
    "postsolve_ms": "Postsolve time ms",
    "verification_ms": "Verification time ms",
}
INTEGER_FIELDS = {
    "variables": "Variables",
    "constraints": "Constraints",
    "nonzeros": "Nonzeros",
    "final_variables": "Final variables",
    "final_constraints": "Final constraints",
    "presolve_reductions": "Presolve reductions",
    "standardized_rows": "Standardized rows",
    "standardized_columns": "Standardized columns",
    "standardized_nonzeros": "Standardized nonzeros",
    "iterations": "Iterations",
    "attempt_count": "Attempt count",
    "dense_memory_budget_bytes": "Dense memory budget bytes",
    "sparse_refactorizations": "Sparse refactorizations",
    "sparse_pivots": "Sparse pivots",
    "sparse_lexicographic_solves": "Sparse lexicographic solves",
}
FLOAT_FIELDS = {
    "pdhg_primal_weight": "PDHG primal weight",
    "estimated_dense_memory_bytes": "Estimated dense memory bytes",
    "primal_residual": "Primal residual",
    "dual_residual": "Dual residual",
    "complementarity_residual": "Complementarity residual",
}
TEXT_FIELDS = {
    "backend": "Backend",
    "presolve_termination": "Presolve termination",
    "fallback_reason": "Fallback reason",
    "dense_memory_guard": "Dense memory guard",
    "sparse_bland_fallback": "Sparse Bland fallback",
}


def field(output: str, label: str) -> str | None:
    match = re.search(rf"(?im)^{re.escape(label)}:\s*(.*?)\s*$", output)
    return match.group(1) if match else None


def run_once(solver: Path, instance: Path, method: str, presolve: bool,
             limit: int, timeout: float, presolve_time_ms: float = 0.0,
             backend: str = "cpu") -> dict:
    command = [str(solver), "--input", str(instance), "--method", method,
               "--max-iterations", str(limit), "--backend", backend]
    if not presolve:
        command.append("--no-presolve")
    elif presolve_time_ms > 0:
        command.extend(["--presolve-time-ms", str(presolve_time_ms)])
    start = time.perf_counter_ns()
    try:
        proc = subprocess.run(command, capture_output=True, text=True,
                              timeout=timeout, check=False)
        output = proc.stdout + "\n" + proc.stderr
        record = {key: field(output, label) for key, label in TIMING_FIELDS.items()}
        record.update({key: field(output, label) for key, label in INTEGER_FIELDS.items()})
        record.update({key: field(output, label) for key, label in FLOAT_FIELDS.items()})
        record.update({key: field(output, label) for key, label in TEXT_FIELDS.items()})
        record["presolve_time_budget_ms"] = field(output, "Presolve time budget ms")
        record.update({
            "status": field(output, "Status"),
            "objective": field(output, "Objective"),
            "verification": field(output, "Verification"),
            "fallback": field(output, "Presolve fallback"),
            "message": field(output, "Message"),
            "exit_code": proc.returncode,
            "wall_ms": (time.perf_counter_ns() - start) / 1e6,
            "presolve_passes": [line for line in re.findall(r"(?im)^Presolve pass:\s*[^\r\n]+$", output)],
        })
        return record
    except subprocess.TimeoutExpired as exc:
        return {"status": "TIMEOUT", "wall_ms": (time.perf_counter_ns() - start) / 1e6,
                "partial_output": (exc.stdout or b"").decode(errors="replace")
                if isinstance(exc.stdout, bytes) else (exc.stdout or "")}


def aggregate(records: list[dict]) -> dict:
    fields = set().union(*(record.keys() for record in records)) if records else set()
    medians = {}
    for key in fields:
        values = []
        for record in records:
            try:
                values.append(float(record[key]))
            except (KeyError, TypeError, ValueError):
                pass
        if values:
            medians[key] = statistics.median(values)
    statuses = {}
    for record in records:
        status = record.get("status", "UNKNOWN")
        statuses[status] = statuses.get(status, 0) + 1
    return {"repetitions": len(records), "status_counts": statuses,
            "median": medians, "raw_runs": records}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", action="append", required=True,
                        help="label=path; repeat for before and after builds")
    parser.add_argument("--instances", nargs="+", default=list(DEFAULT_INSTANCES))
    parser.add_argument("--method", action="append", default=None)
    parser.add_argument("--compare-presolve", action="store_true",
                        help="run both presolve enabled and disabled")
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--max-iterations", type=int, default=500)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--backend", choices=("auto", "cpu", "cuda"), default="cpu",
                        help="execution backend passed to each solver run")
    parser.add_argument("--presolve-time-ms", type=float, default=0.0,
                        help="optional C++ presolve budget (0 leaves it unlimited)")
    parser.add_argument("--output", type=Path, default=ROOT / "benchmarks/lp_audit_baseline.json")
    args = parser.parse_args()
    methods = args.method or ["revised-simplex"]
    if args.repetitions < 1 or args.max_iterations < 1 or args.timeout <= 0:
        parser.error("repetitions, iteration limit, and timeout must be positive")
    solvers = {}
    for entry in args.solver:
        label, separator, value = entry.partition("=")
        path = Path(value).resolve()
        if not separator or not label or not path.is_file():
            parser.error(f"invalid --solver {entry!r}; expected label=existing_executable")
        solvers[label] = path
    unknown = set(args.instances) - set(DEFAULT_INSTANCES)
    if unknown:
        parser.error(f"unknown local instance(s): {', '.join(sorted(unknown))}")

    results = {}
    for label, solver in solvers.items():
        for instance_name in args.instances:
            instance = DEFAULT_INSTANCES[instance_name]
            presolve_modes = [True, False] if args.compare_presolve else [True]
            for method in methods:
                for presolve in presolve_modes:
                    samples = [run_once(solver, instance, method, presolve,
                                        args.max_iterations, args.timeout, args.presolve_time_ms,
                                        args.backend)
                              for _ in range(args.repetitions)]
                    key = f"{label}/{instance_name}/{method}/presolve_{'on' if presolve else 'off'}"
                    results[key] = aggregate(samples)
                    median = results[key]["median"]
                    print(f"{key}: status={results[key]['status_counts']} "
                          f"solve_ms={median.get('solve_ms')} "
                          f"presolve_ms={median.get('presolve_ms')}", flush=True)

    payload = {
        "purpose": "LP audit measurements; this harness does not assert an improvement",
        "timestamp_local": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "system": platform.platform(),
        "python": platform.python_version(),
        "solver_builds": {label: str(path) for label, path in solvers.items()},
        "instances": {name: str(DEFAULT_INSTANCES[name]) for name in args.instances},
        "method": methods,
        "backend": args.backend,
        "repetitions": args.repetitions,
        "max_iterations_per_phase": args.max_iterations,
        "timeout_seconds_per_run": args.timeout,
        "results": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"Wrote {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
