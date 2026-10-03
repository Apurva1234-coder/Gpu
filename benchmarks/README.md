# Benchmarks and datasets

For a single file containing the complete text of all project audit and benchmark reports, see [PROJECT_REPORTS.md](../PROJECT_REPORTS.md). This index remains the short guide to inputs and individual source records.

Use the [dataset-by-dataset benchmark report](DATASET_BENCHMARK_REPORT.md) as the entry point for every model under `datasets/`. It identifies the input, model dimensions where available, recorded solve outcome, verification, timing scope, and detailed evidence. A dataset file is not automatically a successful benchmark: unsupported inputs, timeouts, and cases without a solver run are labeled explicitly.

## Reports

- [Dataset-by-dataset benchmark report](DATASET_BENCHMARK_REPORT.md) — all 10 checked-in `datasets/` models, including non-results and timeouts.
- [Final verified benchmark report](FINAL_SOLVER_BENCHMARK_REPORT.md) — repeated Release measurements for AFIRO, FLUGPL, and QPLIB_9002, with environment, verification, and reference limitations.
- [LP/MILP final optimization report](LP_MILP_FINAL_OPTIMIZATION_REPORT.md) — implementation/measurement scope, controlled comparisons, and medium/large MILP timeout records.
- [LP audit](LP_AUDIT.md) — historical LP method, presolve, sparse-route, and CPU/CUDA experiments; diagnostic runs are not all successful solves.
- [MILP baseline](milp_baseline_20261003.md) — historical runs and bottleneck findings. Some entries use Debug builds or external process limits and must not be compared directly with the final Release measurements.
- [AFIRO repeated-run detail](LP_SMALL_FULL_BENCHMARK_20261003.md) and [QPLIB_9002 QP detail](QP_SMALL_BASELINE_AND_FIX_20261003.md).

The JSON alongside the final benchmark documents contains the machine-readable records. The LP/MILP diagnostics have a separate [optimization result JSON](results/LP_MILP_FINAL_OPTIMIZATION_20261003.json). Earlier audit JSON files in this directory are retained as research history and should be read together with `LP_AUDIT.md`, not as current final scorecards.

## Inputs and provenance

The local demonstration/benchmark inputs are organized by family and size under [`../datasets/`](../datasets/):

```text
datasets/
├── lp/{small,medium,large}/
├── milp/{small,medium,large}/
└── qp/{small,medium,large}/
```

The LP test cases include Netlib models. FLUGPL is identified as the MIPLIB reference case in the final report; other MILP files are included as additional benchmark inputs. QPLIB-formatted inputs are stored under `datasets/qp/`. The benchmark runner also has suite-specific corpus directories (`netlib`, `miplib`, `mittelmann`, and `qplib`); their presence does not mean every instance in those suites has a final measurement. The checked-in benchmark reports state which exact files were run.

Some Netlib downloads use EMPS-compressed `.mps` or `.mps.txt` representations. The current parser rejects the supplied compressed `25fv47.mps` and `80bau3b.mps` files because the available conversion path cannot demonstrate coefficient fidelity. No lossy conversion is silently used. A standard MPS copy must be verified before either is benchmarked.

The benchmark runner does not download datasets. Keep input corpora separate from generated results; the default generated `results/` directory is ignored by Git. Do not overwrite the checked-in `results/` final JSON records with a fresh run—choose a separate output directory for new experiments.

## Reproduce

Build the native Release executable first. Example command from the repository root on Windows:

```powershell
python -m sovereign_solver.benchmark --dataset examples --input examples --solver cpp_solver/build-route-cpu/sovereign_presolve_cli.exe --tier quick --time-limit 60
```

For a specific family, pass the corresponding input directory and dataset name supported by `python -m sovereign_solver.benchmark --help`. Use fixed inputs, build type, method, backend, time limits, and verification settings when comparing repeated runs. Record solver compute time separately from process or web-request wall time.
