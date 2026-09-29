# Sovereign Optimization Solver

A C++17 and Python optimization solver prototype with CPU algorithms and an optional NVIDIA CUDA numerical backend. It includes linear-programming methods, a convex quadratic-programming path, and foundational MILP tools. The C++ solver is the main optimization executable; the Python package provides model parsing, validation, classification, and presolve utilities.

This is an experimental project, not yet a production replacement for mature solvers. Use the limitations below when choosing models and interpreting results. Contributions and reproducible benchmarks are welcome.

## Capabilities

### Linear programming (LP)

- Revised-simplex-style method (`revised-simplex`)
- Dual simplex (`dual-simplex`)
- Mehrotra interior-point method (`ipm`)
- Linear presolve, solution checks, and MPS input

The current revised simplex implementation uses a dense tableau internally and is intended for small examples and development. It is not yet a sparse, large-scale revised-simplex implementation.

### Numerical linear algebra (NLA)

- Reusable vector operations and row-major dense matrix operations
- CSR sparse matrix storage with `A*x` and `Aᵀ*x`
- Partial-pivoting LU and SPD-checked Cholesky factorization
- Dense linear and KKT solve interfaces with dimension, pivot, finiteness, and residual checks
- Centralized numerical tolerances

The LP, QP, and dual-simplex paths use the shared CPU linear-solve layer; revised simplex and Mehrotra IPM also use shared vector operations. CSR is available as a reusable primitive, while current solver model construction still uses dense working matrices in several paths.

### Quadratic programming (QP)

- QP classification and Hessian convexity check
- Positive-definite, positive-semidefinite, and indefinite Hessian classification
- Newton-barrier / interior-point path for supported convex QPs (`qp`)
- Small diagonal-quadratic examples in `examples/`

The QP path is experimental. General cross-term Hessians, free-variable transformations, full QP presolve/postsolve, and comprehensive KKT verification are not yet supported across all model forms. Non-convex QPs are rejected; the solver does not optimize them.

### Mixed-integer linear programming (MILP)

- LP relaxation, preserving binary bounds `[0, 1]`
- Basic serial branch-and-bound (`milp`)
- Gomory cutting-plane path for supported pure-integer rows (`cutting-plane`)
- Feasibility Pump heuristic (`feasibility-pump`)

Branch-and-bound uses a simple best-bound search and first-fractional-variable branching. Gomory cut generation accepts only rows that pass its integer-validity checks; mixed-integer Gomory cuts and broader cut families are not implemented. Feasibility Pump is a heuristic: failure to find a candidate is not proof of MILP infeasibility. Cutting planes and the heuristic are not integrated into branch-and-bound.

## Repository layout

```text
cpp_solver/
  include/       C++ model, LP, QP, presolve, and MILP components
  src/main.cpp   Command-line application
  tests/         C++ regression tests
examples/        LP, QP, MILP, and MPS example inputs
sovereign_solver/ Python parsing, validation, classification, and presolve package
tests/           Python tests
```

## Build the C++ solver

Requirements: a C++17 compiler and CMake 3.16 or newer.

```bash
cmake -S cpp_solver -B cpp_solver/build
cmake --build cpp_solver/build --config Release
```

The executable is `cpp_solver/build/sovereign_presolve_cli` (Linux/macOS) or `cpp_solver/build/Release/sovereign_presolve_cli.exe` (Windows multi-configuration generators).

## Run the Sovereign web dashboard

The optional local dashboard is a thin FastAPI adapter around the existing C++ executable. It does not implement optimization in Python or JavaScript. A valid upload starts the automatic pipeline: parse, validate, classify, presolve, deterministic solver/backend selection, solve, postsolve, and original-model verification. The central `webui/solver_policy.py` selects only implemented paths: revised simplex for automatic continuous LP, Newton/Barrier for QP, and branch-and-bound for MILP. Expert Mode exposes the implemented manual controls for technical use.

From the repository root on Windows:

```powershell
python -m venv .venv-web
.\.venv-web\Scripts\python.exe -m pip install -r webui\requirements.txt
.\.venv-web\Scripts\python.exe -m uvicorn webui.app:app --host 127.0.0.1 --port 8000
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000). Build the C++ targets first. The dashboard supports the repository's `.mps`, `.json`, and `.txt` inputs, including Netlib EMPS/FMPS `.mps.txt` streams. EMPS is expanded in the job directory with the checked-in Netlib reference decoder before the existing parser and C++ solver run. Uploads are limited to 12 MB; expanded MPS output is limited to 96 MB. The adapter uses safe temporary files and subprocess argument arrays and enforces a configurable server-side time limit.

Available API routes:

```text
GET  /api/health
GET  /api/device
GET  /api/capabilities
POST /api/analyze
POST /api/examples/{lp|qp|milp}
POST /api/solve
GET  /api/solve/{id}
GET  /api/results/{id}
GET  /api/benchmarks
```

`solver_time_ms` is always sourced from the C++ CLI's `Solve time ms` field. `backend_total_time_ms` is separately reported by the adapter and is never shown as solver time.

Run the C++ tests with:

```bash
ctest --test-dir cpp_solver/build --output-on-failure
```

Run the Python tests from the repository root with:

```bash
python -m unittest discover -s tests -v
```

## Run the C++ CLI

Examples below use the Linux executable path; substitute the Windows `.exe` path when needed.

```bash
# LP with revised simplex (default)
./cpp_solver/build/sovereign_presolve_cli --input examples/lp.json --method revised-simplex

# LP with dual simplex or interior point
./cpp_solver/build/sovereign_presolve_cli --input examples/afiro.mps --method dual-simplex
./cpp_solver/build/sovereign_presolve_cli --input examples/afiro.mps --method ipm

# Convex QP analysis and solve attempt
./cpp_solver/build/sovereign_presolve_cli --input examples/convex_qp.json --method qp

# MILP LP relaxation, choosing the LP method
./cpp_solver/build/sovereign_presolve_cli --input examples/milp_relaxation.json --method lp-relaxation --lp-method revised-simplex

# Basic branch-and-bound
./cpp_solver/build/sovereign_presolve_cli --input examples/milp_relaxation.json --method milp --lp-method revised-simplex

# Gomory cutting-plane loop
./cpp_solver/build/sovereign_presolve_cli --input examples/milp_relaxation.json --method cutting-plane --max-cuts 100 --max-iterations 100

# Feasibility Pump heuristic
./cpp_solver/build/sovereign_presolve_cli --input examples/milp_relaxation.json --method feasibility-pump --max-fp-iterations 100 --fp-tolerance 1e-7
```

The CLI prints model and presolve information before method-specific output. `--lp-method` applies to MILP relaxation and branch-and-bound. The cutting-plane path currently uses revised simplex.

## Input formats

### JSON

The C++ parser accepts the project's simple JSON model format. Here is a small MILP example:

```json
{
  "name": "Small MILP",
  "objective_sense": "minimize",
  "variables": [
    {"name": "x", "type": "integer"},
    {"name": "y", "type": "binary"}
  ],
  "objective": {"x": -3, "y": -2},
  "constraints": [
    {"name": "capacity", "coefficients": {"x": 2, "y": 2}, "operator": "<=", "rhs": 3}
  ]
}
```

Variable types are `continuous`, `integer`, or `binary`. The C++ JSON reader is intentionally lightweight; use the supplied examples as the format reference. JSON bound-field support is limited, so use the text format or MPS bounds where supported.

Diagonal Hessian entries can be supplied as `quadratic_terms`, for example `{"x": 2}`. The C++ QP solver interprets this as `Qxx = 2` in `1/2 xᵀQx + cᵀx`. The separate generic objective evaluator currently uses the stored diagonal value as the coefficient of `x²`, so do not assume those two paths report the same quadratic objective for arbitrary coefficients. This convention mismatch is a known limitation; cross terms are not generally supported by the QP solve path.

### Plain text

The Python parser accepts simple text models such as:

```text
name: Production plan
objective: maximize 40x + 60y
var x: continuous
var y: integer
constraint: labor: 2x + 4y <= 100
constraint: material: 3x + 2y <= 80
bound: x [0, inf]
```

### MPS

The project includes MPS examples such as `examples/afiro.mps`, `examples/adlittle.mps`, and `examples/agg.mps`. The Python MPS reader supports common linear sections and integer markers. The C++ reader supports a basic linear MPS subset. MPS quadratic sections are not supported.

## Python model analysis

The Python command parses and classifies a model and can run Python presolve; it does not call the C++ optimization algorithms:

```bash
python -m sovereign_solver.cli --input examples/lp.json --presolve
```

## Example data and benchmarking

Small LP, QP, and MILP fixtures are included for functional tests. The Netlib-derived MPS files are useful parser and solver smoke tests. The streaming benchmark runner below provides controlled measurements when run with fixed inputs, hardware, tolerances, and stopping criteria; the included smoke data is not sufficient to claim comparative performance.

### Streaming benchmark runner

The benchmark runner processes one local instance at a time, calls the existing C++ solver CLI, and verifies every returned primal against the original model, including variable bounds, row feasibility, integrality, and objective value. It records unsupported inputs and failures instead of counting them as successful solves. The Python runner is independent from the solver engine; optional HiGHS comparison uses `highspy` only when installed.

Build first, then run:

```powershell
python -m sovereign_solver.benchmark --dataset netlib --input benchmarks/netlib --solver cpp_solver/build/Release/sovereign_presolve_cli.exe --tier quick --time-limit 60
python -m sovereign_solver.benchmark --dataset miplib --input benchmarks/miplib --solver cpp_solver/build/Release/sovereign_presolve_cli.exe --limit 10 --method milp --compare-highs --compare-presolve
python -m sovereign_solver.benchmark --dataset qplib --input benchmarks/qplib --solver cpp_solver/build/Release/sovereign_presolve_cli.exe --method qp --compare-backends
```

Tiers are `quick` (up to 10 files), `standard` (up to 50), and `full` (all discovered files); `--limit N` overrides a tier. The default time limit is 60 seconds per solver invocation. Results go to `results/` by default and can be redirected with `--output-dir`. Useful options include `--method auto|revised-simplex|dual-simplex|ipm|qp|milp|lp-relaxation|cutting-plane|feasibility-pump`, `--backend cpu|cuda|auto`, `--objective-tol`, `--feasibility-tol`, `--compare-highs`, `--compare-presolve`, and `--compare-backends`. LP methods are passed as `--lp-method` within the existing branch-and-bound path for MILPs.

Suggested local input layout:

```text
benchmarks/
  netlib/{small,medium,large}/       # MPS LP instances
  miplib/{small,medium,hard}/        # MPS MILP instances
  qplib/{convex,semidefinite,nonconvex}/
  mittelmann/{lp,qp,other}/
results/                             # generated output; keep inputs separate
```

Supported model files are this project's JSON/text models and the implemented linear MPS subset (`.mps`). Gzip-compressed linear MPS (`.mps.gz`) is supported by the benchmark parser. The web dashboard also accepts Netlib EMPS/FMPS `.mps.txt` inputs and expands them with the checked-in Netlib reference decoder. The current QP interface supports diagonal quadratic objectives in project JSON/text models; standard QPLIB and quadratic MPS encodings are not parsed and are recorded as `UNSUPPORTED` with the parser reason. Mittelmann instances are only run when they use a compatible supported input. The workspace currently contains a local standard-MPS AFIRO fixture, compressed Netlib EMPS files, and gzip MIPLIB MPS files; it contains no `.qplib` instances or compatible Mittelmann inputs. No datasets are downloaded automatically. Place locally obtained files under the corresponding family folders; do not put generated results under benchmark input trees.

The runner emits class-specific LP/MILP/QP CSV and JSON, an all-instance dataset JSON, `external_solver_comparison.csv/json`, `cpu_gpu_benchmark.csv/json`, `presolve_benchmark.csv/json`, `numerical_robustness.csv/json`, and `benchmark_summary.md`. HiGHS is optional and currently compared on LP/MILP; absent HiGHS is recorded as `NOT_AVAILABLE`. CUDA is optional; unsupported methods are labeled and CPU execution continues. CUDA event timings and memory-per-process are not exposed by the current solver CLI, so those fields remain empty and are not inferred from wall-clock time.

Examples available in the repository can be benchmarked immediately with `--dataset examples --input examples`. For example, the included `afiro.mps`, `lp.json`, and `milp_relaxation.json` permit a small smoke run. A timeout is recorded as `TIME_LIMIT`; an optimal status counts as a successful result only after original-model verification passes.

## Current scope

- Revised and dual simplex, presolve, MILP search, cut generation, and solver control logic remain CPU implementations. CUDA accelerates supported numerical kernels in the LP/QP interior-point paths.
- No MIQP, nonlinear optimization, branch-and-cut integration, advanced MILP cuts, parallel search, or learned methods.
- Solver methods are prototypes; numerical robustness and scalability need further work.
- A heuristic result is not an optimality certificate. Interpret each method's status and verification output accordingly.

## License

No license file is currently included. Add a license before redistributing or reusing the project outside its intended scope.
## Optional CUDA backend

The CPU backend remains the default and does not require CUDA. Configure the optional NVIDIA backend with a CUDA Toolkit installation:

```powershell
cmake -S cpp_solver -B build-cuda -DSOVEREIGN_ENABLE_CUDA=ON
cmake --build build-cuda --config Release
build-cuda\sovereign_presolve_cli.exe --device-info
```

The backend provides a reusable CUDA context/stream, cuBLAS AXPY/dot/dense GEMV, cuSPARSE CSR SpMV, and cuSOLVER dense LU solves. Dense KKT solves used by LP/QP interior-point methods run through cuSOLVER when CUDA is selected; equilibration, system assembly, residual checks, and the remaining solver logic run on the CPU. The current matrix/vector API transfers working data for each operation, so this is functional CUDA execution rather than a claim of end-to-end device residency or guaranteed speedup.

Use `--backend cpu|cuda|auto` on solver runs. `cpu` is the default. `cuda` requires a working CUDA build and device, and `--device N` selects the NVIDIA device. `auto` selects CUDA for sufficiently large repeated interior-point workloads; simplex and other CPU-only methods stay on CPU. The CLI prints the selected numerical backend. A CUDA-enabled build requires a C++17 compiler supported by the installed CUDA Toolkit, CMake 3.16+, and NVIDIA cuBLAS, cuSPARSE, and cuSOLVER libraries.

If CUDA is unavailable, configure without `SOVEREIGN_ENABLE_CUDA`; the same CLI reports `CUDA Available: NO` and all existing CPU functionality remains available.
