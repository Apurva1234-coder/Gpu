# LP Audit — Phase 1 baseline

Scope: C++ LP solve path, its dashboard dispatch/fallback, and benchmark telemetry. MILP/QP algorithm changes are out of scope. This is a baseline and architecture audit, not a performance-improvement claim.

## Reproduce

The local corpus used is the checked-in Netlib MPS set. The controlled run compares the existing MSVC Release executable with the instrumented MSVC Release build, uses 3 repetitions, caps each simplex phase at 500 iterations, and times out an individual process after 8 seconds:

```powershell
python scripts/lp_audit_benchmark.py `
  --solver before=cpp_solver/build-route-cpu/sovereign_presolve_cli.exe `
  --solver instrumented=cpp_solver/build-lp-audit-msvc/sovereign_presolve_cli.exe `
  --instances afiro bandm scagr25 pilot --compare-presolve `
  --repetitions 3 --max-iterations 500 --timeout 8 `
  --output benchmarks/lp_audit_baseline.json
```

The 500 limit is intentionally bounded for baseline profiling. It does not represent an unlimited solve, and revised simplex applies the limit per phase. The JSON stores every raw run, statuses, verification, pipeline stages, standardized dimensions, and pass profiles. Timings from this instrumented run must not be read as a solver-speed improvement; the purpose is to identify failure modes and stage costs. External-solver comparison was unavailable: SciPy/HiGHS is not installed in the active Python environment, and no licensed Gurobi/CPLEX setup was verified. CUDA was not part of this CPU baseline.

## Pipeline findings

The production LP request parses and classifies the MPS in `main.cpp`, runs the C++ presolver, then calls one chosen LP class, reconstructs with `postsolve`, and checks the original rows and bounds. The web Auto path selects Revised Simplex for every continuous LP. On a qualifying solver failure, the web layer can run Dual Simplex as another attempt. Separately, the C++ process retries the original model only when a presolved result reports OPTIMAL but fails original-space feasibility verification. The CLI exposes only a yes/no presolve-fallback flag; it does not explain why or list all attempts.

Observed structural risks in the current implementation:

- The C++ presolver allows ten passes. Each pass scans singleton rows, may process only one two-term equality substitution, loops over all rows for each fixed variable, removes rows, and rebuilds active-index mappings. That makes repeated model scans and substitutions a direct candidate for medium/large presolve cost.
- The current LP Auto method is Revised Simplex regardless of actual LP structure. `LPSolver` internally decides dense versus sparse Revised Simplex with a size/density heuristic. Dual Simplex and Mehrotra IPM call dense `standardize()` directly and build dense matrices without using that guard.
- Dense standardization materializes `vector<vector<double>>`, expands equalities into two inequalities, adds finite upper bounds as rows, and can duplicate free variables. The Revised Simplex dense route then allocates a tableau and standard matrix. Mehrotra IPM forms a dense Schur/KKT matrix. Large LPs can therefore enter dense code through manually selected methods.
- The CPU PDHG path builds a sparse matrix, but the baseline 500-iteration runs did not meet all three residual tolerances. The CUDA PDHG implementation uploads its sparse matrix once, but periodically copies iterates back and performs residual checks on the CPU. The current production Auto policy does not select CUDA without measured same-model evidence.
- The standard benchmark folder has small/medium Netlib cases and Pilot, but no genuine million-scale sparse LP. A million-scale MILP ingestion smoke test is not evidence for million-scale LP support.

## Initial measurements

The baseline is intentionally bounded and reports status with timing. These runs do not all reach an optimum:

| Instance | Original rows × columns | Original NNZ | Presolve result | Standardized shape (instrumented) | Result/status |
|---|---:|---:|---|---:|---|
| AFIRO | 27 × 32 | 83 | 2 active rows removed; 2 passes | 35 × 32, 117 NNZ | OPTIMAL, original verification PASS |
| Bandm | 305 × 472 | 2,494 | 10 passes; active model 248 rows × 415 variables, 2,014 NNZ | 496 × 415, 4,028 NNZ | NUMERICAL_FAILURE, verification FAIL |
| Scagr25 | 471 × 500 | 1,554 | 10 passes; active model 337 rows × 489 variables, 1,395 NNZ | 748 × 489, 2,816 NNZ | ITERATION_LIMIT at 500/phase cap |
| Pilot | 1,441 × 3,652 | 43,167 | 7 passes; active model 1,427 rows × 3,441 variables, 41,064 NNZ | 2,696 × 3,441, 45,600 NNZ | NUMERICAL_FAILURE: sparse LU pivot failure |

In the three-repeat baseline, the existing executable returned NUMERICAL_FAILURE for Bandm and Pilot on every run, and ITERATION_LIMIT for Scagr25. The same-model presolve toggle also showed presolve enabled was slower on these bounded cases: Bandm medians were 9.25 ms presolve + 272.70 ms solver pipeline versus 0.31 ms + 84.83 ms without presolve; Scagr25 was 9.83 ms + 170.86 ms versus 0.25 ms + 145.79 ms. These are failed/iteration-limited runs, not optimal-solution speed comparisons. Use the JSON raw data and repeat the same pinned binaries before drawing performance conclusions.

Presolve pass telemetry from the instrumented run showed diminishing returns. For Bandm, pass 1 reduced the combined active variable/row/entry count by 13.24%, pass 2 by 3.14%, and the final six substitutions each reduced it by about 0.15%. Scagr25 reduced 10.22% in pass 1; subsequent passes were each below 0.3%. This supports testing a reduction-versus-time stop policy, while the exact policy should be benchmarked after it is implemented.

## Phase 1 changes

- Added per-pass presolve duration, active variables/rows/stored entries before and after, reductions by kind, and combined model-size reduction percentage.
- Pass-shape counters update from reductions instead of rescanning all rows at the end of every pass. Inactive rows are excluded from active-row presolve transforms and infeasibility checks.
- Added original NNZ, standardized rows/columns/NNZ, standardization time, and a separate optimization time in the CLI output. “Solve pipeline time” retains the full call duration.
- Added parsers to the Python benchmark and web API timing contracts for the new values. CSV output serializes nested profile data as JSON.
- Added a repeatable before/after LP audit harness and committed the raw three-run profile at `benchmarks/lp_audit_baseline.json`.

## Phase 2 changes

- C++ now performs exactly one LP algorithm attempt per process. If an optimal presolved candidate fails original-model verification, it is reported as `NUMERICAL_FAILURE`; the CLI explains that the automatic full-model retry was skipped. The measured timing and iteration count therefore cannot silently include a second algorithm run.
- Web Auto can launch at most one explicit Dual Simplex fallback. It records method, backend, status, original-model verification, total time, per-stage times, iterations, and the reason for each attempt. The automatic request shares one wall-clock budget across both attempts.
- Fallback preserves the same presolve setting. The previous behavior that silently enabled presolve on the retry is removed.
- Dual Simplex fallback is skipped if a conservative estimate for the dense standardized matrix/tableau/basis workspaces exceeds 256 MiB. The skip is returned in the attempt history. This is a fallback-specific guard; manually choosing dense algorithms is still an open memory-safety item.
- The Auto result panel now shows each attempt and its measured time/iterations, including why a fallback was skipped.

The Phase 2 tests cover attempt history, preserved presolve configuration, and dense-fallback memory eligibility. No solver speedup claim is made from this change. Presolve adaptation, comprehensive guards for manually selected dense algorithms, and structure-based algorithm dispatch remain open.
