# Sovereign Mathematical Optimization Solver — V1

This prototype reads one optimization problem file, constructs and classifies its mathematical model, and does not solve it. The filename and extension are ignored for classification.

## Run

```bash
python -m sovereign_solver.cli --input path/to/problem.dat
```

JSON files may use any extension. A problem contains `name`, `objective_sense`, `variables`, `objective`, and `constraints`; add `quadratic_terms` for diagonal quadratic objective terms such as `{"x": 2}` for `2x²`.

Plain-text files are also supported, for example:

```text
name: Production plan
objective: maximize 40x + 60y
var x: continuous
var y: integer
constraint: labor: 2x + 4y <= 100
constraint: material: 3x + 2y <= 80
bound: x [0, inf]
```

Standard MPS files are supported as well, including Netlib-style files:

```bash
python -m sovereign_solver.cli --input examples/afiro
```

The MPS reader handles `NAME`, `ROWS`, `COLUMNS`, `RHS`, `BOUNDS`, and `ENDATA`, including `E`, `L`, and `G` rows, bounds, and integer marker blocks. MPS quadratic sections are reported as unsupported in V1.

Supported classifications are LP, MILP, and QP. MIQP, NLP, and MINLP are rejected or outside scope.

## Test

```bash
python -m unittest discover -s tests -v
```
