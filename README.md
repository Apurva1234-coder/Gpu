# Sovereign Optimization Solver

A CPU-based optimization solver prototype written in C++17 and Python. It includes linear-programming methods, a convex quadratic-programming path, and foundational MILP tools. The C++ solver is the main optimization executable; the Python package provides model parsing, validation, classification, and presolve utilities.

This is an experimental project, not yet a production replacement for mature solvers. Use the limitations below when choosing models and interpreting results. Contributions and reproducible benchmarks are welcome.

## Capabilities

### Linear programming (LP)

- Revised-simplex-style method (`revised-simplex`)
- Dual simplex (`dual-simplex`)
- Mehrotra interior-point method (`ipm`)
- Linear presolve, solution checks, and MPS input

The current revised simplex implementation uses a dense tableau internally and is intended for small examples and development. It is not yet a sparse, large-scale revised-simplex implementation.

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

Small LP, QP, and MILP fixtures are included for functional tests. The Netlib-derived MPS files are useful parser and solver smoke tests, but this repository does not yet provide a controlled performance benchmark suite or a validated speed comparison against established solvers. Benchmark comparisons should use identical instances, hardware, tolerances, and stopping criteria.

## Current scope

- CPU only; no CUDA/GPU backend.
- No MIQP, nonlinear optimization, branch-and-cut integration, advanced MILP cuts, parallel search, or learned methods.
- Solver methods are prototypes; numerical robustness and scalability need further work.
- A heuristic result is not an optimality certificate. Interpret each method's status and verification output accordingly.

## License

No license file is currently included. Add a license before redistributing or reusing the project outside its intended scope.
