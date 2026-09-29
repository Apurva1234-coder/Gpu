from __future__ import annotations

import json
import csv
import io
import os
import platform
import re
import shutil
import subprocess
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any

from fastapi import FastAPI, File, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from sovereign_solver.classification import classify_model
from sovereign_solver.parser import parse_problem_file
from webui.solver_policy import SolverPolicy

ROOT = Path(__file__).resolve().parents[1]
STATIC = Path(__file__).resolve().parent / "static"
MAX_UPLOAD_BYTES = 12 * 1024 * 1024
MAX_EXPANDED_MPS_BYTES = 96 * 1024 * 1024
SOLUTION_ZERO_TOLERANCE = 1e-8
ALLOWED_EXTENSIONS = {".mps", ".json", ".txt"}
JOBS: dict[str, dict[str, Any]] = {}


def _solver_path() -> Path:
    candidates = [
        ROOT / "cpp_solver" / "build" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build" / "Release" / "sovereign_presolve_cli.exe",
        ROOT / "cpp_solver" / "build" / "sovereign_presolve_cli",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("C++ solver executable was not found. Build cpp_solver before starting the web UI.")


def _emps_converter_path() -> Path:
    """Return the locally built Netlib EMPS reference decoder."""
    candidates = [
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


def analyze_model(path: Path) -> dict[str, Any]:
    model = parse_problem_file(str(path))
    classification = classify_model(model)
    variables = model.variables
    constraints = model.constraints
    nonzeros = sum(len(row.coefficients) for row in constraints)
    variable_count = len(variables)
    constraint_count = len(constraints)
    denominator = variable_count * constraint_count
    sparsity = (1 - nonzeros / denominator) * 100 if denominator else 100.0
    variable_index = {variable.name: index for index, variable in enumerate(variables)}
    sample_limit = 5000
    points: list[list[int]] = []
    for row_index, row in enumerate(constraints):
        for variable in row.coefficients:
            if len(points) >= sample_limit:
                break
            points.append([row_index, variable_index[variable]])
        if len(points) >= sample_limit:
            break
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
        "sparsity": sparsity,
        "types": type_counts,
        "bounded_variables": bounded,
        "objective_sense": model.objective_sense,
        "matrix": {
            "rows": constraint_count,
            "columns": variable_count,
            "points": points,
            "aggregated": nonzeros > sample_limit,
        },
        "variable_metadata": [
            {"name": v.name, "type": v.type, "lower": _format_bound(model.bounds.get(v.name, (None, None))[0]), "upper": _format_bound(model.bounds.get(v.name, (None, None))[1])}
            for v in variables
        ],
    }


def _prepare_model(model_path: Path, directory: Path) -> tuple[Path, dict[str, Any]]:
    """Return the actual standard-MPS/JSON input consumed by the solver and its analysis."""
    solver_path = _expand_netlib_emps(model_path, directory) if _is_netlib_emps(model_path) else model_path
    summary = analyze_model(solver_path)
    if solver_path != model_path:
        summary.update({
            "file_name": model_path.name,
            "file_size": model_path.stat().st_size,
            "format": "NETLIB EMPS → MPS",
            "conversion": "Expanded locally with the checked-in Netlib EMPS decoder before analysis and solving.",
        })
    return solver_path, summary


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
        "solver_time_ms": _capture(r"^Solve time ms:\s*([^\r\n]+)$", output, None, float),
        "postsolve_time_ms": _capture(r"^Postsolve time ms:\s*([^\r\n]+)$", output, None, float),
        "verification_time_ms": _capture(r"^Verification time ms:\s*([^\r\n]+)$", output, None, float),
        "backend_total_time_ms": total_ms,
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
    presolve = {
        "before_variables": _capture(r"^Variables:\s*(\d+)$", output, None, int),
        "before_constraints": _capture(r"^Constraints:\s*(\d+)$", output, None, int),
        "after_variables": _capture(r"^Final variables:\s*(\d+)$", output, None, int),
        "after_constraints": _capture(r"^Final constraints:\s*(\d+)$", output, None, int),
        "reductions": _capture(r"^Presolve reductions:\s*(\d+)$", output, None, int),
    }
    metrics = {
        "objective": _capture(r"^Objective:\s*([^\r\n]+)$", output, None, float),
        "iterations": _capture(r"^Iterations:\s*(\d+)$", output, None, int),
        "nodes_created": _capture(r"^Nodes Created:\s*(\d+)$", output, None, int),
        "nodes_processed": _capture(r"^Nodes Processed:\s*(\d+)$", output, None, int),
        "nodes_pruned": _capture(r"^Nodes Pruned:\s*(\d+)$", output, None, int),
        "primal_bound": _capture(r"^Primal Bound:\s*([^\r\n]+)$", output, None, float),
        "dual_bound": _capture(r"^Dual Bound:\s*([^\r\n]+)$", output, None, float),
        "absolute_gap": _capture(r"^Absolute Gap:\s*([^\r\n]+)$", output, None, float),
        "relative_gap": _capture(r"^Relative Gap:\s*([^\r\n]+)$", output, None, float),
        "primal_residual": _capture(r"^Primal residual:\s*([^\r\n]+)$", output, None, float),
        "dual_residual": _capture(r"^Dual residual:\s*([^\r\n]+)$", output, None, float),
        "feasibility": _capture(r"^Feasibility:\s*([^\r\n]+)$", output, None, float),
        "iteration_limit": _capture(r"^Iteration limit:\s*(.+)$", output, None),
        "backend": _capture(r"^Backend:\s*(.+)$", output, "cpu"),
        "method": _capture(r"^METHOD:\s*(.+)$", output, None),
        "message": _capture(r"^Message:\s*(.+)$", output, None),
        "convexity": _capture(r"^Hessian:\s*(.+)$", output, None),
    }
    return {"status": status, "verification": verification, "timings": timings, "metrics": metrics, "presolve": presolve, "variables": variables, "raw_log": output[-16000:]}


def _solution_variables(parsed: list[dict[str, Any]], metadata: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Map solver values to original variable names without turning missing data into zero."""
    returned = {str(row.get("name")): row.get("value") for row in parsed if row.get("name") is not None}
    rows: list[dict[str, Any]] = []
    for meta in metadata:
        name = str(meta["name"])
        value = returned.get(name)
        lower, upper = meta.get("lower"), meta.get("upper")
        numeric_value = isinstance(value, (int, float)) and not isinstance(value, bool)
        at_lower = numeric_value and lower is not None and abs(float(value) - float(lower)) <= SOLUTION_ZERO_TOLERANCE
        at_upper = numeric_value and upper is not None and abs(float(value) - float(upper)) <= SOLUTION_ZERO_TOLERANCE
        variable_type = str(meta.get("type") or "continuous")
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


def device_info() -> dict[str, Any]:
    try:
        completed = subprocess.run([str(_solver_path()), "--device-info"], capture_output=True, text=True, timeout=10, check=False)
        text = completed.stdout.strip()
    except (OSError, subprocess.TimeoutExpired, FileNotFoundError) as exc:
        text = f"CUDA status unavailable: {exc}"
    cuda_available = bool(re.search(r"CUDA Available:\s*YES", text, re.I))
    gpu_count = _capture(r"GPU Count:\s*(\d+)", text, 0, int)
    return {
        "cpu": platform.processor() or platform.machine() or "CPU information unavailable",
        "threads": os.cpu_count() or 1,
        "cuda_available": cuda_available,
        "gpu_count": gpu_count,
        "gpu_name": _capture(r"GPU(?: Name)?:\s*(.+)$", text, None),
        "raw": text,
    }


class SolveRequest(BaseModel):
    job_id: str
    method: str = Field(default="auto", pattern="^(auto|revised-simplex|dual-simplex|ipm|qp|milp|lp-relaxation|cutting-plane|feasibility-pump)$")
    backend: str = Field(default="auto", pattern="^(auto|cpu|cuda)$")
    presolve: bool = True
    max_iterations: int = Field(default=10000, ge=0, le=10_000_000)
    time_limit_seconds: int = Field(default=60, ge=1, le=3600)


class AutoSolveRequest(BaseModel):
    job_id: str


app = FastAPI(title="Sovereign Optimization API", version="1.0")
app.add_middleware(CORSMiddleware, allow_origins=["http://localhost:8000", "http://127.0.0.1:8000"], allow_methods=["*"], allow_headers=["*"])


@app.get("/api/health")
def health() -> dict[str, Any]:
    try:
        solver = str(_solver_path())
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
        "max_iterations": {"default": 10000, "zero_means_unlimited": True},
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
        solver_path, summary = _prepare_model(model_path, directory)
    except ValueError as exc:
        shutil.rmtree(directory, ignore_errors=True)
        raise HTTPException(422, f"Model parsing failed: {exc}") from exc
    JOBS[job_id] = {"directory": directory, "path": solver_path, "source_path": model_path, "analysis": summary, "state": "analyzed"}
    return {"job_id": job_id, "analysis": summary}


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
        solver_path, summary = _prepare_model(destination, directory)
    except ValueError as exc:
        shutil.rmtree(directory, ignore_errors=True)
        raise HTTPException(422, f"Model parsing failed: {exc}") from exc
    JOBS[job_id] = {"directory": directory, "path": solver_path, "source_path": destination, "analysis": summary, "state": "analyzed"}
    return {"job_id": job_id, "analysis": summary}


def _run_solver(job: dict[str, Any], configuration: dict[str, Any]) -> dict[str, Any]:
    info = device_info()
    if configuration["backend"] == "cuda" and not info["cuda_available"]:
        raise HTTPException(409, "CUDA backend is unavailable on this system.")
    try:
        solver = _solver_path()
    except FileNotFoundError as exc:
        raise HTTPException(503, str(exc)) from exc
    command = [str(solver), "--input", str(job["path"]), "--backend", configuration["backend"]]
    if configuration["method"] != "auto":
        command.extend(["--method", configuration["method"]])
    if not configuration["presolve"]:
        command.append("--no-presolve")
    if configuration["method"] not in {"qp", "milp", "cutting-plane", "feasibility-pump", "lp-relaxation"}:
        command.extend(["--max-iterations", str(configuration["max_iterations"])])
    started = time.perf_counter()
    try:
        process = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=configuration["time_limit_seconds"], check=False)
        total_ms = (time.perf_counter() - started) * 1000
        raw_output = (process.stdout or "") + (("\nSTDERR:\n" + process.stderr) if process.stderr else "")
        result = parse_solver_output(raw_output, total_ms)
        if process.returncode != 0 and result["status"] == "FAILED":
            result["metrics"]["message"] = result["metrics"]["message"] or "Solver process failed."
    except subprocess.TimeoutExpired as exc:
        total_ms = (time.perf_counter() - started) * 1000
        text = (exc.stdout or "") if isinstance(exc.stdout, str) else ""
        result = parse_solver_output(text, total_ms)
        result["status"] = "TIME LIMIT"
        result["metrics"]["message"] = f"The solver reached the configured {configuration['time_limit_seconds']}-second time limit."
    return result


def _complete_result(job_id: str, job: dict[str, Any], configuration: dict[str, Any], result: dict[str, Any], automation: dict[str, Any]) -> dict[str, Any]:
    result.update({
        "job_id": job_id,
        "analysis": job["analysis"],
        "configuration": configuration,
        "automation": automation,
        "pipeline": ["parse", "validate", "presolve", "solve", "postsolve", "verify"],
    })
    result["variables"], result["variable_summary"] = _solution_variables(result["variables"], job["analysis"]["variable_metadata"])
    job["state"], job["result"] = "complete", result
    return result


@app.post("/api/solve")
def solve(request: SolveRequest) -> dict[str, Any]:
    job = JOBS.get(request.job_id)
    if not job:
        raise HTTPException(404, "Unknown or expired job ID. Analyze a model first.")
    configuration = request.model_dump()
    result = _run_solver(job, configuration)
    return _complete_result(request.job_id, job, configuration, result, {"mode": "expert", "attempts": [{"method": configuration["method"], "backend": configuration["backend"], "status": result["status"], "verification": result["verification"]}]})


@app.post("/api/solve/auto")
def solve_automatically(request: AutoSolveRequest) -> dict[str, Any]:
    job = JOBS.get(request.job_id)
    if not job:
        raise HTTPException(404, "Unknown or expired job ID. Analyze a model first.")
    selection = SolverPolicy.select(job["analysis"], device_info())
    configuration = {"job_id": request.job_id, **selection.payload()}
    configuration.pop("fallback_methods")
    result = _run_solver(job, configuration)
    attempts = [{"method": configuration["method"], "backend": configuration["backend"], "status": result["status"], "verification": result["verification"], "reason": "Initial automatic selection"}]
    if selection.fallback_methods and SolverPolicy.can_fallback(result["status"], result["verification"]):
        fallback_method = selection.fallback_methods[0]
        fallback_configuration = {**configuration, "method": fallback_method}
        fallback = _run_solver(job, fallback_configuration)
        attempts.append({"method": fallback_method, "backend": fallback_configuration["backend"], "status": fallback["status"], "verification": fallback["verification"], "reason": f"Fallback after {result['status']} / verification {result['verification']}"})
        result, configuration = fallback, fallback_configuration
    automation = {"mode": "automatic", "selection": selection.payload(), "attempts": attempts, "final_method": configuration["method"], "final_backend": configuration["backend"]}
    return _complete_result(request.job_id, job, configuration, result, automation)


@app.get("/api/solve/{job_id}")
@app.get("/api/results/{job_id}")
def get_result(job_id: str) -> dict[str, Any]:
    job = JOBS.get(job_id)
    if not job:
        raise HTTPException(404, "Unknown job ID.")
    return {"job_id": job_id, "state": job["state"], "analysis": job["analysis"], "result": job.get("result")}


@app.get("/api/results/{job_id}/export")
def export_result(job_id: str, format: str = "csv") -> Response:
    job = JOBS.get(job_id)
    result = job.get("result") if job else None
    if result is None:
        raise HTTPException(404, "A completed result was not found for this job.")
    rows = result.get("variables", [])
    if format == "json":
        payload = json.dumps({"job_id": job_id, "variable_summary": result.get("variable_summary"), "variables": rows}, indent=2)
        return Response(payload, media_type="application/json", headers={"Content-Disposition": f'attachment; filename="sovereign_solution_{job_id}.json"'})
    if format != "csv":
        raise HTTPException(422, "Export format must be csv or json.")
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=["name", "value", "type", "lower", "upper", "at_bound", "fractional"])
    writer.writeheader()
    writer.writerows(rows)
    return Response(output.getvalue(), media_type="text/csv", headers={"Content-Disposition": f'attachment; filename="sovereign_solution_{job_id}.csv"'})


@app.get("/api/benchmarks")
def benchmarks() -> dict[str, Any]:
    datasets: list[dict[str, Any]] = []
    for directory in sorted((ROOT / "results").glob("*")):
        report = directory / f"{directory.name}_benchmark.json"
        if not report.is_file():
            continue
        try:
            data = json.loads(report.read_text(encoding="utf-8"))
            instances = data.get("instances", [])
            datasets.append({
                "dataset": directory.name,
                "metadata": data.get("metadata", {}),
                "instances": [{
                    "instance": row.get("instance"), "status": row.get("status"),
                    "objective": row.get("objective"), "verification_pass": row.get("verification_pass"),
                    "iterations": row.get("iterations"), "nodes_processed": row.get("nodes_processed"),
                    "solve_time_ms": row.get("solve_time_ms"), "failure_reason": row.get("failure_reason"),
                } for row in instances],
            })
        except (OSError, json.JSONDecodeError):
            continue
    return {"datasets": datasets}


@app.get("/")
def index() -> FileResponse:
    return FileResponse(STATIC / "index.html")


app.mount("/", StaticFiles(directory=STATIC, html=True), name="static")
