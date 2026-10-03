# Consolidated Project and Benchmark Reports

> One-file reference containing the full text of the project audit and benchmark reports. The source reports remain in their original locations. No benchmark values have been recalculated or changed for this compilation.

## How to read this compilation

- Start with the dataset-by-dataset report and final benchmark report for the latest documented measured outcomes.
- Later appendices include historical diagnostics, implementation audits, and earlier benchmark snapshots. Their builds, limits, datasets, and timing scopes differ; do not combine them into one comparison.
- Timeout, unsupported, and unmeasured cases remain visible. A report that has no result does not imply a successful or failed solve.
- Appendix contents are copied in full. Heading levels and repository-relative Markdown links are adjusted only so navigation works in this combined file.

## Contents

- [Appendix 1: Dataset Benchmark Report](#appendix-1-dataset-benchmark-report)
- [Appendix 2: Final Solver Benchmark Report](#appendix-2-final-solver-benchmark-report)
- [Appendix 3: Lp Milp Final Optimization Report](#appendix-3-lp-milp-final-optimization-report)
- [Appendix 4: Lp Small Full Benchmark 20261003](#appendix-4-lp-small-full-benchmark-20261003)
- [Appendix 5: Milp Baseline 20261003](#appendix-5-milp-baseline-20261003)
- [Appendix 6: Qp Small Baseline And Fix 20261003](#appendix-6-qp-small-baseline-and-fix-20261003)
- [Appendix 7: Lp Audit](#appendix-7-lp-audit)
- [Appendix 8: Audit Report](#appendix-8-audit-report)
- [Appendix 9: Benchmarks](#appendix-9-benchmarks)
- [Appendix 10: Improvements](#appendix-10-improvements)

## Appendix 1: Dataset Benchmark Report

Source document: [benchmarks/DATASET_BENCHMARK_REPORT.md](benchmarks/DATASET_BENCHMARK_REPORT.md)

## Dataset-by-dataset benchmark report

This report inventories every model currently checked into `datasets/` and ties it to the saved benchmark evidence. It records only measured outcomes. **Not benchmarked** means no completed solve measurement was found in the checked-in reports; it does not imply that the model solves or fails. Timeout and unsupported outcomes are retained as results.

The detailed records are dated 2026-10-03 and were produced on Windows with an MSVC Release CPU build. The summary table in the final benchmark report distinguishes solver-only time from fresh CLI process wall time. Historical diagnostics may use different builds, limits, or timing scopes; those are labeled below and must not be compared as a controlled speed ranking.

### Summary

| Dataset | Family / size | Model dimensions | Current documented outcome |
|---|---|---:|---|
| [AFIRO](#lp-afiro) | LP / small | 32 variables, 27 rows, 83 nonzeros | OPTIMAL, original-model verification PASS |
| [25FV47](#lp-25fv47) | LP / medium | Not parsed by the current input path | UNSUPPORTED input encoding; no solve result |
| [80BAU3B](#lp-80bau3b) | LP / large | Not parsed by the current input path | UNSUPPORTED input encoding; no solve result |
| [FLUGPL](#milp-flugpl) | MILP / small | 18 variables, 18 rows, 46 nonzeros | OPTIMAL, verification PASS; 5,367 nodes |
| [TIMTAB1](#milp-timtab1) | MILP / small | 397 variables, 171 rows, 829 nonzeros | TIME_LIMIT_NO_FINAL_RESULT in a 30 s diagnostic; no incumbent recorded |
| [traininstance2](#milp-traininstance2) | MILP / medium | 12,890 variables, 15,603 rows, 41,531 nonzeros | TIME_LIMIT_NO_INCUMBENT at 30 s during root LP; 0 branch nodes |
| [supportcase6](#milp-supportcase6) | MILP / large | 130,052 variables, 771 rows, 584,976 nonzeros | TIME_LIMIT_NO_INCUMBENT at 60 s during root LP; 0 branch nodes |
| [QPLIB_9002](#qp-qplib_9002) | QP / small | 2,890 variables, 1,649 rows, 9,690 linear nonzeros, 2,890 diagonal Hessian terms | OPTIMAL, original-model KKT PASS |
| [QPLIB_8906](#qp-qplib_8906) | QP / medium | 5,223 variables, 838 rows, 15,558 linear nonzeros, 56,975 Hessian terms | UNSUPPORTED off-diagonal Hessian; no solve run |
| [QPLIB_10038](#qp-qplib_10038) | QP / large | 160,800 variables, 160,400 rows, 959,203 linear nonzeros | Parsed structurally; no solver run or timing recorded |

### LP: AFIRO

- **Input:** [`datasets/lp/small/afiro.mps`](datasets/lp/small/afiro.mps), standard MPS; 3,327 bytes.
- **Model:** 32 continuous variables, 27 rows, 83 constraint nonzeros; minimize.
- **Result:** OPTIMAL in 16 revised-simplex iterations. Objective `-464.75314285714285`; original-model feasibility verification PASS.
- **Timing:** median internal solver time **0.13135 ms** and median fresh CLI wall time **16.6138 ms** over 10 runs. The difference includes process startup and output capture; use the solver time for optimizer timing.
- **Presolve:** 27×32 / 83 nonzeros → 25×32 / 81 nonzeros; two singleton reductions and two bound tightenings.
- **Reference context:** HiGHS 1.15.1 and Gurobi 13.0.3 returned the same objective on this model. Their timings have different API/process scopes and are not a controlled speed ranking.
- **Source note:** the timed final suite uses `benchmarks/netlib/small/afiro.mps` (3,244 bytes). Its parsed optimization model matches the `datasets/` AFIRO copy; the report’s SHA-256 is for the benchmark-copy file.
- **Full evidence:** [AFIRO benchmark report](benchmarks/LP_SMALL_FULL_BENCHMARK_20261003.md), [final benchmark report](benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md), [raw AFIRO run record](benchmarks/results/lp_small_full_benchmark.json).

### LP: 25FV47

- **Input:** [`datasets/lp/medium/25fv47.mps`](datasets/lp/medium/25fv47.mps), 70,477 bytes.
- **Result:** The current repository parser identifies this file as Netlib EMPS-compressed MPS and rejects it because coefficient fidelity is not verified. No optimization solve, status, objective, or timing is claimed.
- **Next step:** provide a trusted standard-MPS copy, verify its parsed coefficients against the source, then add a bounded benchmark record. Do not treat a decode attempt as a solver result.

### LP: 80BAU3B

- **Input:** [`datasets/lp/large/80bau3b.mps`](datasets/lp/large/80bau3b.mps), 298,952 bytes.
- **Result:** The current repository parser identifies this file as Netlib EMPS-compressed MPS and rejects it because coefficient fidelity is not verified. No optimization solve, status, objective, or timing is claimed.
- **Next step:** provide a trusted standard-MPS copy and validate the conversion before benchmarking it. The `large` directory label alone is not evidence of a completed large-LP solve.

### MILP: FLUGPL

- **Input:** [`datasets/milp/small/flugpl.mps`](datasets/milp/small/flugpl.mps), 4,161 bytes; also present as the same-hash benchmark fixture under `benchmarks/miplib/small/`.
- **Model:** 18 variables (11 integer), 18 rows, 46 constraint nonzeros.
- **Result:** OPTIMAL in the final 5/5 runs; objective, incumbent, and best bound `1,201,500`; absolute and relative gap 0; original-model feasibility/integrality verification PASS.
- **Search:** median 5,367 processed nodes and 105,334 LP iterations in the final benchmark report. The later presolve-integrated optimization report records 96,253 LP iterations; these are separate report runs/configurations and must not be merged into one timing row.
- **Timing:** final benchmark report median solver **269.2061 ms**, fresh CLI wall **286.485 ms** (five runs).
- **Reference context:** the final report records objective agreement with HiGHS and Gurobi. HiGHS used 89 nodes; Gurobi presolved this case to an empty model. This demonstrates a large search-efficiency gap, not a claim that Sovereign is faster.
- **Full evidence:** [final benchmark report](benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md), [MILP optimization report](benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [historical MILP baseline](benchmarks/milp_baseline_20261003.md), [machine-readable result](benchmarks/results/FINAL_SOLVER_BENCHMARK_20261003.json).

### MILP: TIMTAB1

- **Input:** [`datasets/milp/small/timtab1.mps`](datasets/milp/small/timtab1.mps), 53,477 bytes.
- **Model:** 397 variables, 171 rows, 829 constraint nonzeros; the parser identifies 171 integer variables.
- **Result:** a 30 s diagnostic is recorded as `TIME_LIMIT_NO_FINAL_RESULT`; an earlier 15 s external process cap also expired. No incumbent or final status was recorded in those runs. Therefore no optimality, infeasibility, or performance claim is made.
- **Historical diagnostics:** the baseline report contains an earlier 15 s externally terminated run. These are diagnostic attempts under different runs/limits, not one combined measurement.
- **Evidence:** [MILP baseline](benchmarks/milp_baseline_20261003.md), [MILP optimization report](benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [diagnostic JSON](benchmarks/results/LP_MILP_FINAL_OPTIMIZATION_20261003.json).

### MILP: traininstance2

- **Input:** [`datasets/milp/medium/traininstance2.mps`](datasets/milp/medium/traininstance2.mps), 2,707,382 bytes.
- **Model:** 12,890 variables (7,880 integer), 15,603 rows, 41,531 constraint nonzeros.
- **Result:** `TIME_LIMIT_NO_INCUMBENT` at the recorded **30,000 ms** CPU run limit, while solving the root LP relaxation. One LP solve was entered; zero branch-and-bound nodes were processed; no incumbent, root bound, gap, or optimal solution is available.
- **Historical context:** the baseline also records a separately timed root-LP-only attempt and earlier runs with different time limits/build configurations. Those details are in the baseline report and should not be mixed with the 30 s final diagnostic.
- **Evidence:** [MILP baseline](benchmarks/milp_baseline_20261003.md), [MILP optimization report](benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [diagnostic JSON](benchmarks/results/LP_MILP_FINAL_OPTIMIZATION_20261003.json).

### MILP: supportcase6

- **Input:** [`datasets/milp/large/supportcase6.mps`](datasets/milp/large/supportcase6.mps), 25,426,441 bytes.
- **Model:** 130,052 variables, 771 rows, 584,976 constraint nonzeros. The repository parser marks all variables integer.
- **Result:** `TIME_LIMIT_NO_INCUMBENT` at the recorded **60,000 ms** CPU run limit, during the root LP relaxation. One LP solve was entered; zero branch-and-bound nodes were processed; no incumbent, root bound, gap, or optimal solution is available.
- **Historical context:** the baseline’s older “parse/analyze only” result is not a solve benchmark. The final 60 s result is the run to use for the present solve-status summary.
- **Evidence:** [MILP baseline](benchmarks/milp_baseline_20261003.md), [MILP optimization report](benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [diagnostic JSON](benchmarks/results/LP_MILP_FINAL_OPTIMIZATION_20261003.json).

### QP: QPLIB_9002

- **Input:** [`datasets/qp/small/QPLIB_9002.qplib`](datasets/qp/small/QPLIB_9002.qplib), 203,064 bytes; QPLIB DCL.
- **Model:** convex minimization; 2,890 variables, 1,649 equality rows, 9,690 linear nonzeros; diagonal Hessian with 2,890 terms.
- **Result:** OPTIMAL in 43 Newton iterations and 43 linear-system solves, with 7,059 accumulated PCG iterations. Objective `5,698,097,498.146622`; original-model KKT verification PASS.
- **Timing:** final report median solver **377.4874 ms**, fresh CLI wall **455.0647 ms** over five runs. A separate QP optimization report uses a different run set and timing median; see that report rather than combining the values.
- **Backend:** the documented final solve ran on CPU. No QP reference solve completed in the report environment; no QP solver comparison is claimed.
- **Full evidence:** [QP baseline and fix report](benchmarks/QP_SMALL_BASELINE_AND_FIX_20261003.md), [final benchmark report](benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md), [machine-readable result](benchmarks/results/FINAL_SOLVER_BENCHMARK_20261003.json).

### QP: QPLIB_8906

- **Input:** [`datasets/qp/medium/QPLIB_8906.qplib`](datasets/qp/medium/QPLIB_8906.qplib), 1,017,151 bytes.
- **Structure:** 5,223 variables, 838 rows, 15,558 linear terms, 56,975 Hessian terms (1,941 diagonal and 55,034 off-diagonal).
- **Result:** explicitly unsupported by the current QP input/solve path because general off-diagonal Hessian terms are not implemented. It was structurally inspected, not submitted as a solver run. No solver timing or objective is available.
- **Evidence:** [final benchmark report, medium-QP limitation](benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md#medium-qp-limitation), [machine-readable report](benchmarks/results/FINAL_SOLVER_BENCHMARK_20261003.json).

### QP: QPLIB_10038

- **Input:** [`datasets/qp/large/QPLIB_10038.qplib`](datasets/qp/large/QPLIB_10038.qplib), 22,029,643 bytes.
- **Structure:** the repository’s Python QPLIB parser reads 160,800 variables, 160,400 rows, 959,203 linear terms, and 801 diagonal quadratic objective terms.
- **Result:** no solver run is recorded. These parser-level dimensions do not establish that the native QP algorithm can solve this large model. No status, objective, verification, or timing is claimed.

### Reproduction and timing rules

The final measured instances can be rerun with the commands in the [final benchmark report](benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md) and the [LP/MILP optimization report](benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md). Use a Release build, preserve the input files, record the selected method/backend and run limits, and verify returned solutions against the original model.

- **Solver time** is the native solver's internal measurement.
- **CLI wall time** includes launching the process and capturing its output.
- Web backend/request time also includes adapter preparation and orchestration; it is a separate measurement scope.
- Timeouts are not successful solves. A timeout without incumbent has no objective or optimality certificate.
- `UNSUPPORTED`, `TIME_LIMIT`, and **not benchmarked** are distinct states.

### Related benchmark material

- [Final verified LP/MILP/QP report](benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md)
- [LP/MILP final optimization and timeout report](benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md)
- [LP audit report and historical algorithm experiments](benchmarks/LP_AUDIT.md)
- [MIPLIB/FLUGPL historical baseline](benchmarks/milp_baseline_20261003.md)
- [AFIRO detailed repeated-run report](benchmarks/LP_SMALL_FULL_BENCHMARK_20261003.md)
- [QPLIB_9002 detailed QP report](benchmarks/QP_SMALL_BASELINE_AND_FIX_20261003.md)
- [Machine-readable benchmark results](benchmarks/results)

## Appendix 2: Final Solver Benchmark Report

Source document: [benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md](benchmarks/FINAL_SOLVER_BENCHMARK_REPORT.md)

## Sovereign Optimization Solver — Benchmark Report

**Date:** 2026-10-03 · **Git HEAD:** `365209b88701d889d74e7e44a5ec20524782dbcf` · **Branch:** `solver-performance-telemetry`

### Executive Summary

The current Release CLI solved and verified three representative instances: Netlib AFIRO (LP), MIPLIB FLUGPL (MILP), and QPLIB_9002 (convex diagonal-Q QP). LP and MILP objectives matched installed HiGHS and Gurobi references. No completed reference QP result was available: Gurobi’s restricted license rejected the 2,890-variable model, and a HiGHS QP API trial returned no solution during the trial window.

The FLUGPL result is correct, but it required 5,367 processed nodes; HiGHS used 89, while Gurobi presolved the model to empty. Sovereign’s MILP presolve telemetry is currently not applied to the branch-and-bound input. QPLIB_9002 passed original-model KKT verification in 43 Newton iterations on CPU. QPLIB_8906 remains unsupported because it contains off-diagonal Hessian entries. These are prototype measurements, not broad production-performance claims.

### Environment and Method

| Item | Value |
|---|---|
| OS | Windows NT 10.0.26200.0 |
| CPU / logical processors | AMD64 Family 25 Model 68 Stepping 1 (AuthenticAMD) / 16 |
| RAM | 16,989,736,960 bytes (~15.82 GiB) |
| GPU present | NVIDIA GeForce RTX 3050 Laptop GPU, 4 GiB, driver 592.00 |
| Backend for these runs | CPU; GPU not used |
| Compiler / build | MSVC 14.29.30133, CMake Release, Ninja, `/O2 /Ob2 /DNDEBUG` |
| Executable | `cpp_solver/build-route-cpu/sovereign_presolve_cli.exe`, 479,232 bytes, 2026-10-03 13:21 local |

The worktree was dirty at start with pre-existing README, solver, test, and web UI changes. Those changes were preserved. The existing Release CLI was up-to-date; no solver source was changed or rebuilt. Two stale C++ test binaries were rebuilt in Release.

Measurements use the native CLI, not the website. LP ran 10 times; MILP and QP ran five times each. Solver-only timing is the CLI’s internal timer. Fresh-process wall time is measured externally with a monotonic timer and includes process startup and output capture. Reference solver, API, and fresh Python-process timings are shown separately; their scopes differ, so no cross-scope speed ranking is made. The QP timing distribution uses normal concise CLI output; a separate `--print-primal` run was used for independent objective reconstruction.

Pipeline: `Input → Native parser → Validation/classification → Presolve or QP transformation → Scaling → Solver → Postsolve → Original-model verification → Result/telemetry`.

### LP — Netlib AFIRO

**Input:** `benchmarks/netlib/small/afiro.mps`; MPS; 3,244 bytes; SHA-256 `fd3562804ff19382a9cd8bcb22ec81bffd24a4143a2290783831d8c64516a24b`. Netlib describes its LP data directory as MPS test problems and includes AFIRO ([Netlib LP data](https://www.netlib.org/lp/data/)).

| Model statistic | Value |
|---|---:|
| Rows / columns / linear NNZ / density | 27 / 32 / 83 / 9.6065% |
| Continuous / integer | 32 / 0 |
| Constraints | 8 equality, 19 `<=`, 0 `>=` or ranged |
| Bounds | 32 lower-only (default 0); none free, upper-only, double-bounded, or fixed |
| Objective | Minimize; constant 0; 5 nonzero linear coefficients (-0.6 to 10) |
| Nonzero `|Aij|` / finite `|RHS|` range | 0.107–2.429 / 0–500 |
| Validation | Native parser PASS; no non-finite values, empty rows, duplicate column-row entries, or bad row references |

**Presolve:** 27×32, 83 NNZ → 25×32, 81 NNZ (2 rows, 2 NNZ removed; 7.407% rows, 0% columns, 2.410% NNZ). Measured: 2 singleton reductions, 2 bound tightenings, no fixed variables, substitutions, or redundant rows. Two passes.

**Result:** Revised Simplex, CPU, 16 iterations, OPTIMAL in 10/10 runs. Solver objective and independent original-model objective: `-464.75314285714285` (difference 0). Maximum row violation `5.68e-14`; bound violation 0; verification PASS. Dual/KKT metrics are N/A for this output path.

| Timing (10 runs) | Min ms | Median ms | Mean ms | Max ms | Sample SD ms |
|---|---:|---:|---:|---:|---:|
| Solver-only | 0.1265 | 0.1314 | 0.1330 | 0.1439 | 0.0059 |
| Fresh CLI wall | 15.0726 | 16.6138 | 17.4072 | 25.6459 | 3.0574 |

AFIRO’s process startup/output dominates end-to-end time. Standardized dense memory estimate was 81,400 bytes under the 256 MiB budget. Phase-I/II counts, pivots, scale-factor ranges, and peak RSS are unavailable.

| Reference | Version | Status / objective | Method / iterations | Median reported / API wall | Median fresh Python process |
|---|---|---|---|---:|---:|
| HiGHS | 1.15.1 | Optimal / -464.75314285714285 | Automatic simplex / 6 | 0.7043 / 0.7259 ms | 340.519 ms |
| Gurobi | 13.0.3 | Optimal / -464.75314285714285 | Automatic / 2 | ~1 / 0.4278 ms | 188.859 ms |

Gurobi presolve returned 6 rows, 9 columns, 26 NNZ. HiGHS reported reductions, but exact dimensions were unavailable from the API used.

### MILP — MIPLIB FLUGPL

**Input:** `datasets/milp/small/flugpl.mps`; MPS; 4,161 bytes; SHA-256 `ed4b1fcb07d26b513027721d61344c2c4371cb506bf66098df328047c183b0c0`. This is the established small FLUGPL fixture used by the repository regression test.

**Model:** 18 variables (11 integer, 7 continuous), 18 constraints, 46 linear NNZ; minimize. **Reported presolve:** 18×18/46 → 16×17/41; 1 fixed variable, 2 bound tightenings, 1 redundant row, 1 singleton row, 0 substitutions. The CLI’s aggregate `Presolve reductions: 6` sums counters and counts the fixed variable again as eliminated; use individual counts and net dimensions.

**Execution-path limitation:** the CLI calls `BranchAndBound::solve(model, ...)` with the original model, not the returned `red.model`. The reported MILP presolve reductions therefore do not reduce this branch-and-bound tree.

**Result:** Branch-and-Bound, CPU; Revised Simplex node LPs; pseudocost/strong-branching score with fractional tie-break and best-bound-first node selection. OPTIMAL in 5/5 runs; objective, incumbent, and bound all 1,201,500; absolute/relative gap 0. Nodes created/processed/pruned: 5,483/5,367/2,742. LP solves/iterations: 5,367/105,334. First incumbent: node 3,504 at about 150 ms. Original feasibility/integrality verification PASS (max row `1.42e-14`, bound 0, integrality `2.13e-14`).

| Timing (5 runs) | Min ms | Median ms | Mean ms | Max ms | Sample SD ms |
|---|---:|---:|---:|---:|---:|
| MILP solver | 265.4822 | 269.2061 | 271.8991 | 283.7694 | 7.2668 |
| Fresh CLI wall | 284.6613 | 286.4850 | 289.9096 | 302.0967 | 7.0836 |

| Reference | Version | Status / objective | Nodes / LP iterations | Median reported / API wall | Median fresh Python process |
|---|---|---|---|---:|---:|
| HiGHS | 1.15.1 | Optimal / 1,201,500 | 89 / 858 | 175.482 / 175.517 ms | 509.602 ms |
| Gurobi | 13.0.3 | Optimal / 1,201,500 | 0 / 0 | ~1 / 0.974 ms | 192.621 ms |

Gurobi presolve returned 0 rows, 0 columns, 0 NNZ (solved during presolve). HiGHS presolved dimensions were unavailable. Both references explored substantially fewer nodes; this is a material prototype limitation, not a claim of competitiveness.

### QP — QPLIB_9002

**Input:** `datasets/qp/small/QPLIB_9002.qplib`; QPLIB DCL; 203,064 bytes; SHA-256 `e6aea2b85e49862df5d8ad03339f65506ff149eeadaa74bb25ebacad14f7a664`.

| Model statistic | Value |
|---|---:|
| Variables / rows / linear NNZ | 2,890 / 1,649 / 9,690 |
| Hessian NNZ | 2,890 diagonal; 0 off-diagonal; diagonal range 9.288328e-12–2.0 |
| Objective | Minimize; constant 0; no linear coefficients |
| Constraints / bounds | 1,649 equalities; 2,163 upper-only and 727 double-bounded variables |
| Convexity | Convex; positive semidefinite diagonal Hessian |
| Generic presolve | 0 reductions; original dimensions retained |

QP preparation uses variable/row/objective transformation and scaling, not LP-style presolve. From code and model structure, 727 double bounds add one upper slack and equality each: expected transformed system 3,617 variables, 2,376 rows. This dimension count is derived; CLI does not print it.

**Result and original-model KKT:** QP Newton Barrier, CPU; OPTIMAL, 43 Newton iterations, 43 linear solves, 7,059 PCG iterations. Objective `5,698,097,498.1466217`; independent objective from returned vector `5,698,097,498.146628`; difference `6.68e-6` absolute (`1.17e-15` relative). Scaled primal residual `7.59021848e-9`; max raw equality-row error `0.0024293` at `c386` before row normalization; bound violation 0; dual/stationarity `2.43429411e-16`; complementarity `4.69230348e-16`; KKT PASS.

Representative stage times: parse 28.90 ms; generic presolve 6.79 ms; convexity 0.08 ms; transformation 6.98 ms; scaling 0.44 ms; initialization 1.49 ms; Newton assembly 5.53 ms; linear solve 356.38 ms; postsolve 0.47 ms; KKT verification 21.27 ms. Linear-system work dominates solver time. QP solver time excludes postsolve/KKT verification.

| Timing (5 normal-output runs) | Min ms | Median ms | Mean ms | Max ms | Sample SD ms |
|---|---:|---:|---:|---:|---:|
| Full QP solver time | 359.1185 | 377.4874 | 374.1448 | 386.3437 | 11.2976 |
| Fresh CLI wall | 442.6786 | 455.0647 | 454.8807 | 467.0831 | 11.6404 |

No completed reference QP result: Gurobi 13.0.3’s restricted license rejected 2,890 variables (2,000-variable limit); HiGHS 1.15.1 accepted a diagonal-QP construction but returned no result during the trial window, so it was stopped. CPLEX is not installed. No objective/timing comparison is claimed.

### Medium QP Limitation

QPLIB_8906 was structurally inspected only, not submitted to the solver. File: `datasets/qp/medium/QPLIB_8906.qplib`, 1,017,151 bytes, SHA-256 `29dd7c0295e04610d14b1c1abc6a0c8184400d2cbfc3b584695557cdc768976c`. It has 5,223 variables, 838 constraints, 15,558 linear NNZ, and 56,975 Hessian terms (1,941 diagonal, 55,034 off-diagonal). Current QPLIB parsing explicitly rejects off-diagonal Hessian terms. General sparse Hessian support and medium/large QP validation are future work.

### Combined Results

| Problem | Dataset | Model: vars / rows / linear NNZ / Q NNZ / integer | Status | Objective | Verification | Median solver / CLI wall |
|---|---|---|---|---:|---|---:|
| LP | AFIRO | 32 / 27 / 83 / N/A / 0 | OPTIMAL | -464.75314285714285 | PASS | 0.1314 / 16.6138 ms |
| MILP | FLUGPL | 18 / 18 / 46 / N/A / 11 | OPTIMAL | 1,201,500 | PASS | 269.2061 / 286.4850 ms |
| QP | QPLIB_9002 | 2,890 / 1,649 / 9,690 / 2,890 diag / 0 | OPTIMAL | 5,698,097,498.1466217 | KKT PASS | 377.4874 / 455.0647 ms |

| Type | Original → reduced/prepared rows | Original → reduced/prepared cols | Original → reduced/prepared NNZ |
|---|---:|---:|---:|
| LP | 27 → 25 | 32 → 32 | 83 → 81 |
| MILP | 18 → 16 (reported; not consumed by B&B) | 18 → 17 (reported; not consumed by B&B) | 46 → 41 (reported; not consumed by B&B) |
| QP | 1,649 → 2,376 transformed* | 2,890 → 3,617 transformed* | 9,690 → 11,144 linear entries*; Q NNZ 2,890 |

`*` QP transformation dimensions are derived from the 727 finite double bounds and implementation, not emitted directly.

### Current Limitations

- The verified QP path is CPU-only here and accepts diagonal Hessians; off-diagonal sparse Hessians are rejected. QPLIB_8906 and medium/large QP are not validated.
- Only one MILP instance is measured; its tree is much larger than HiGHS/Gurobi’s. Current CLI presolve reductions are not passed to branch-and-bound.
- The benchmark covers one representative model per family and is not production-wide performance evidence.
- Unavailable telemetry: exact HiGHS presolved dimensions, LP dual/KKT metrics, simplex phase split and dense pivots/refactorizations, scale-factor ranges, presolve attempted counts/history sizes, and peak RSS.
- No successful QP reference result. Gurobi is license-limited; the HiGHS QP trial returned no solution.

### Demonstrated Capabilities

Native C++17 CLI parsing and CPU execution for these MPS LP/MILP and diagonal-Q QPLIB cases; revised-simplex LP; branch-and-bound with incumbent/bound/gap/node/LP telemetry; diagonal-Q Newton-barrier QP; presolve and scaling telemetry; postsolve; original-model LP/MILP verification; original-model QP KKT verification. The machine has an NVIDIA GPU and optional CUDA infrastructure exists in the repository, but these runs do not demonstrate GPU acceleration.

### Reproduction and Tests

Run from repository root in PowerShell:

```powershell
.\cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input .\benchmarks\netlib\small\afiro.mps --method revised-simplex --backend cpu
.\cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input .\datasets\milp\small\flugpl.mps --method milp --lp-method revised-simplex --backend cpu
.\cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input .\datasets\qp\small\QPLIB_9002.qplib --method qp --backend cpu
```

For independent QP objective reconstruction, add `--print-primal`; this prints 2,890 values and was excluded from QP wall samples. Release rebuild command for this checkout (CLI was already up-to-date):

```text
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -C "cpp_solver\build-route-cpu" sovereign_presolve_cli
```

```text
ctest --test-dir cpp_solver/build-route-cpu --output-on-failure  # 10/10 passed
.\.venv-web\Scripts\python.exe -m unittest discover -s tests -p 'test_*.py' -v  # 59/59 passed
```

`pytest` is not installed; all unittest-discoverable Python tests passed without installing dependencies. All three native models were rerun after tests and were OPTIMAL with verification PASS.

### Conclusion

The measured instances support correctness for AFIRO, FLUGPL, and QPLIB_9002. The strongest next investigations are MILP tree size and connecting reported presolve reductions to the B&B model; for QPLIB_9002, linear-system work dominates. No optimization was performed in this reporting task.

### Demo Summary

- AFIRO: 32 variables; objective -464.75314285714285; PASS; 0.1314 ms median solver time.
- FLUGPL: 18 variables (11 integer); objective 1,201,500; PASS; 5,367 processed nodes; 269.2061 ms median.
- QPLIB_9002: 2,890 variables; objective 5,698,097,498.1466217; KKT PASS; 43 iterations; 377.4874 ms median.
- LP/MILP objectives agree with tested HiGHS and Gurobi results. No reference QP objective was obtained.
- All Sovereign runs used CPU; no GPU acceleration is claimed.

## Appendix 3: Lp Milp Final Optimization Report

Source document: [benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md](benchmarks/LP_MILP_FINAL_OPTIMIZATION_REPORT.md)

## Sovereign LP / MILP Final Optimization Pass

**Measurement date:** 2026-10-03\
**Source revision tested:** `80e9ca80b1827f4dc90a20557ba67cccad3b0e4e`\
**Working tree:** solver and test edits listed below are uncommitted; existing server log files were already untracked. No commit or push was made.

### Executive summary

The code confirmed that the CLI calculated MILP presolve reductions but passed the original model to branch-and-bound. The pass now sends the integer-safe presolved model into B&B, restores the incumbent to original variable coordinates, and verifies it against the original model before reporting an optimal result. FLUGPL remains optimal and verified.

On ten interleaved FLUGPL runs per configuration, the integrated presolve reduced LP iterations from **105,334 to 96,253 (-8.6%)** and root LP iterations from **16 to 12**. It reduced the model from 18 variables / 18 rows / 46 nonzeros to 17 / 16 / 41. The B&B tree did **not** shrink: both configurations processed 5,367 nodes and solved 5,367 LP relaxations. Median solver time changed from 266.48 ms to 264.76 ms and median CLI wall time from 284.20 ms to 283.81 ms. Those time differences are within measurement variation; this pass does not establish a meaningful FLUGPL speedup.

A separate medium/large MILP failure was also fixed: B&B eagerly allocated a dense root LP before the LP solver could choose its sparse route. After removing that eager dense allocation for models over the existing memory budget, `traininstance2` and `supportcase6` reached sparse revised simplex. They still hit their 30-second and 60-second limits in the root LP without finding an incumbent. This changes an immediate dense-workspace failure into bounded solver progress, not into a completed solve.

Warm basis reuse cut FLUGPL LP iterations by about 79%, but was 15–18% slower in elapsed time on the measured runs. It remains opt-in and is not the default. A root rounding-and-repair experiment found no incumbent on FLUGPL and added an LP solve, so it was removed.

### Baseline and environment

| Item | Measurement |
|---|---|
| Git SHA at start | `80e9ca80b1827f4dc90a20557ba67cccad3b0e4e` |
| Branch | `solver-performance-telemetry` |
| Tracked worktree at start | Clean; existing untracked web server logs preserved |
| OS | Windows 11, build 26200 (`Windows-11-10.0.26200-SP0`) |
| Logical processors | 16 |
| Compiler | MSVC 19.29.30159 (toolset path 14.29.30133) |
| Build | CMake + Ninja, Release, `/O2 /Ob2 /DNDEBUG`, `SOVEREIGN_ENABLE_CUDA=OFF` |
| Release executable | `cpp_solver/build-route-cpu/sovereign_presolve_cli.exe` |
| Python for tests | `.venv-web`, Python 3.13.7 |
| Solve backend | CPU, explicitly requested with `--backend cpu`; no GPU work in these runs |

The cached Release target was rebuilt under the Visual Studio compiler environment before the final runs. The exact CPU model/RAM could not be queried from this restricted process. No LP/MILP/QP tolerances, website code, or solver dependencies were changed.

### Diagnosis and implementation

#### 1. MILP presolve now feeds branch-and-bound

Before the change, `main.cpp` ran `Presolver` and displayed its reductions, then called `BranchAndBound.solve(model, ...)` with the original model. The B&B root relaxation, every child relaxation, and the search tree therefore ignored those reductions.

The CLI now calls the presolve-aware overload. B&B solves the reduced model. Once it has an incumbent, the overload uses the existing fixed-variable and substitution history to reconstruct original coordinates, recomputes the original objective, and checks original bounds, integrality, and constraints. An optimal status is downgraded to numerical failure if that original-model check does not pass. The CLI performs its independent original-model check as well.

The MILP presolve mode protects discrete semantics:

- Singleton bounds on integer variables are rounded inward to integer bounds.
- Empty integer domains, including a nonintegral singleton equality, are recognized as infeasible.
- Equality substitution does not eliminate an integer variable, because that can create an unrepresented congruence condition.
- Fixed variables and continuous-variable substitutions remain reversible through the existing postsolve history.
- Relaxation, feasibility-pump, and verification loops ignore columns/rows that presolve marked inactive.

#### 2. Oversized MILP root relaxations use the existing sparse route

B&B previously called dense `standardize(rootLPModel)` unconditionally. That call could throw the dense workspace guard before `LPSolver` got a chance to dispatch to sparse revised simplex. B&B now prepares and reuses a dense root structure only when the existing memory estimator says it fits the 256 MiB budget. Larger roots pass no dense prepared structure, allowing the existing `LPSolver` sparse route to run.

#### 3. Root bound telemetry

The CLI now prints the root LP bound when the root relaxation is optimal. This makes root-bound and final-gap comparisons visible alongside the existing node, LP iteration, heuristic, cut, and timing metrics.

#### 4. Deliberately not changed

No simplex, QP Newton, branching, pseudocost, cut-generation, or GPU algorithm was rewritten. No website changes were made. The LP code path for ordinary LPs is unchanged.

### FLUGPL controlled comparison

Ten fresh CLI runs were interleaved between the original-model comparator (`--no-presolve`) and the integrated integer-aware presolve configuration. Both used the same Release executable, input, CPU backend, default limits, and method. The comparator reproduces the prior B&B behavior: solve the original MILP without applying MILP presolve to the tree.

| FLUGPL metric | Original model to B&B | Presolved model to B&B | Change |
|---|---:|---:|---:|
| Result / original verification | OPTIMAL / PASS | OPTIMAL / PASS | unchanged |
| Objective | 1,201,500 | 1,201,500 | unchanged |
| Original model | 18 vars / 18 rows / 46 NNZ | 18 / 18 / 46 | unchanged |
| Active model after presolve | 18 vars / 18 rows / 46 NNZ | 17 / 16 / 41 | -1 var, -2 rows, -5 NNZ |
| Root LP bound | 1,167,185.7255923203 | 1,167,185.7255923203 | unchanged |
| Root LP iterations | 16 | 12 | -25% |
| B&B nodes processed | 5,367 | 5,367 | unchanged |
| Nodes created / pruned | 5,483 / 2,742 | 5,483 / 2,742 | unchanged |
| LP solves | 5,367 | 5,367 | unchanged |
| Total LP iterations | 105,334 | 96,253 | **-8.6%** |
| Median node-LP time | 193.32 ms | 182.73 ms | -5.5% |
| Median node-model update time | 43.28 ms | 49.46 ms | +14.3% |
| First incumbent | node 3,504; 150.84 ms median | node 3,504; 148.42 ms median | same node; timing not material |
| Median solver time | 266.48 ms | 264.76 ms | -0.65% |
| Median fresh CLI wall | 284.20 ms | 283.81 ms | -0.14% |
| Final relative gap | 0 | 0 | unchanged |

Timing evidence is noisy: one presolve-on sample was 367 ms, and medians are effectively equal. The defensible result is fewer simplex iterations with the same tree and correctness—not a claimed wall-time win. Presolve reduced one variable, two rows, and five nonzeros. It tightened two bounds, fixed one variable, removed one redundant row and one implied singleton row, and performed no substitutions.

FLUGPL did not benefit from earlier incumbent discovery. Root rounding was attempted but rejected, the feasibility pump used six LP solves over seven iterations without finding an incumbent, and the first incumbent still appeared at node 3,504. No cuts were generated or accepted.

#### Warm-start versus cold-start

Five-run FLUGPL samples with integrated presolve:

| Metric | Cold | Warm enabled |
|---|---:|---:|
| LP iterations | 96,253 | 19,876 (-79.4%) |
| Warm starts attempted / accepted / failed | 0 / 0 / 0 | 5,366 / 4,885 / 481 |
| Median solver time | 262.00 ms | 310.39 ms (+18.5%) |
| Median CLI wall | 283.57 ms | 327.40 ms (+15.4%) |
| Nodes processed | 5,367 | 5,367 |

The current warm state reuses a parent simplex tableau/basis and attempts dual reoptimization on child bounds. It materially reduces pivot iterations, but state reconstruction and reoptimization cost more wall time on FLUGPL. Warm starts stay opt-in. No sparse-LU, eta-update, or factorization architecture was added.

### LP and QP regression checks

| Dataset | Before | After | Result |
|---|---|---|---|
| Netlib AFIRO LP | 10 runs: median 0.1274 ms solver, 16.88 ms wall; 16 iterations | 10 runs: median 0.1251 ms solver, 16.13 ms wall; 16 iterations | OPTIMAL, PASS, objective -464.75314285714285; no LP algorithm changed |
| Netlib ADLITTLE LP | — | 140 iterations, 1.0302 ms solver, 23.98 ms wall | OPTIMAL, PASS, objective 225,494.96316237721 |
| Netlib AGG LP | — | 119 iterations, 15.2615 ms solver, 45.87 ms wall | OPTIMAL, PASS, objective -35,991,767.286576472 |
| QPLIB_9002 QP | — | 43 Newton iterations, 364.26 ms solver, 450.40 ms wall | OPTIMAL, KKT PASS; objective 5,698,097,498.1466217 |

AFIRO row/column/NNZ statistics were 27/32/83 before presolve and 25/32/81 after it. Its objective and iteration count did not change. The QP algorithm was not modified; its final primal residual was `7.59021848e-9`, dual residual `2.43429411e-16`, and complementarity `4.69230348e-16`.

### Additional MILP diagnostics

The sparse-root fix was checked against the medium and large repository MILPs with the requested bounded budgets. These did not reach B&B because their root LPs did not finish in time.

| Dataset | Structure | Presolve / root LP | Bounded result |
|---|---|---|---|
| `traininstance2.mps` (medium) | 12,890 vars, 15,603 rows, 41,531 NNZ | 12 vars fixed; 12,878 active vars; root sparse revised simplex ran 2,370 iterations | `TIME_LIMIT_NO_INCUMBENT` at 30 s; no B&B nodes |
| `supportcase6.mps` (large) | 130,052 vars, 771 rows, 584,976 NNZ | no presolve reductions; root sparse revised simplex ran 865 iterations | `TIME_LIMIT_NO_INCUMBENT` at 60 s; no B&B nodes |
| `timtab1.mps` (small-tier MIPLIB file) | 397 vars, 171 rows, 829 NNZ | baseline parse/presolve completed; 13 variables were fixed | 30-second diagnostic expired without a final solver result; not used in the performance comparison |

Before the sparse-root fix, `traininstance2` and `supportcase6` exited after parsing/presolve with a dense-workspace refusal. Afterward they return controlled time-limit statuses with LP iteration progress. They still need faster sparse root relaxations and, for larger instances, an incumbent heuristic that can find feasible integer points. A timeout is not an unsupported-model claim.

The repository contains no `p0033`, `bell3a`, `egout`, `enigma`, `mod008`, or `stein27` files, so none were added or downloaded.

### B&B strategy and what remains

- **Branching:** existing pseudocost score (`down estimate × up estimate`) with a fractional-violation tie-break; no branching change was made.
- **Node queue:** existing best-bound-first selection; no node-selection change was made.
- **Root bound/cuts:** FLUGPL root bound was 1,167,185.7255923203; the final objective/bound was 1,201,500. No cuts were generated, so root bound strength did not change.
- **Incumbent heuristics:** current root rounding and feasibility pump did not improve FLUGPL's first incumbent. A single root rounding/continuous reoptimization experiment also failed to find an incumbent on FLUGPL and added an LP solve, so it was removed.
- **Warm LP reoptimization:** iteration count improves sharply but wall time regresses on FLUGPL; needs lower per-start overhead or a selective policy before becoming default.
- **Remaining FLUGPL bottleneck:** 5,367-node search tree and 96,253 LP iterations remain; presolve did not reduce the tree.
- **Remaining medium/large bottleneck:** sparse root revised simplex did not finish within 30/60 seconds. These runs spent their budgets in the root LP and produced no incumbent or B&B progress.
- **LP numerical/performance work:** AFIRO, ADLITTLE, and AGG passed. This pass did not change LP pivoting or factorization; only MILP root dispatch now allows large relaxations to reach the existing sparse route.
- **Coverage limitation:** these are prototype results on a small set of checked-in instances, not a production solver certification.

#### Reference solver data

HiGHS and Gurobi were not installed or discoverable in this runtime, so no new reference run was made. The repository’s earlier benchmark report records independent historical comparisons for AFIRO and FLUGPL (tested on an earlier revision): HiGHS 1.15.1 used 89 nodes on FLUGPL; Gurobi 13.0.3 presolved that instance to empty. Those are prior measurements, not results from this run. They are reported as context only; no solver ranking is claimed.

### Validation and reproduction

| Validation | Result |
|---|---|
| Release C++ build | Passed with MSVC `/O2 /Ob2 /DNDEBUG` |
| C++ tests | 10/10 passed |
| Python `unittest` | 59/59 passed using `.venv-web` |
| AFIRO native CLI | OPTIMAL, PASS |
| FLUGPL native CLI | OPTIMAL, original-model verification PASS |
| QPLIB_9002 native CLI | OPTIMAL, original-model KKT PASS |
| `git diff --check` | Passed |

Build command from the repository root:

```powershell
cmake --build cpp_solver/build-route-cpu --config Release
```

Representative commands from the measured run:

```powershell
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\milp\small\flugpl.mps --method milp --backend cpu
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\milp\small\flugpl.mps --method milp --backend cpu --no-presolve
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\milp\small\flugpl.mps --method milp --backend cpu --warm-start 1
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\milp\medium\traininstance2.mps --method milp --backend cpu --time-limit-ms 30000 --max-nodes 100000
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\milp\large\supportcase6.mps --method milp --backend cpu --time-limit-ms 60000 --max-nodes 100000
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\lp\small\afiro.mps --method revised-simplex --backend cpu
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\qp\small\QPLIB_9002.qplib --method qp --backend cpu
```

### Files changed

Solver/test changes are limited to:

- `cpp_solver/include/presolve/Presolver.hpp`
- `cpp_solver/include/milp/BranchAndBound.hpp`
- `cpp_solver/include/milp/LPRelaxation.hpp`
- `cpp_solver/include/milp/FeasibilityPump.hpp`
- `cpp_solver/src/main.cpp`
- `cpp_solver/tests/presolve_tests.cpp`
- `cpp_solver/tests/milp_branch_and_bound_tests.cpp`

The corresponding machine-readable run data is in [`LP_MILP_FINAL_OPTIMIZATION_20261003.json`](benchmarks/results/LP_MILP_FINAL_OPTIMIZATION_20261003.json). No solver changes were committed or pushed.

## Appendix 4: Lp Small Full Benchmark 20261003

Source document: [benchmarks/LP_SMALL_FULL_BENCHMARK_20261003.md](benchmarks/LP_SMALL_FULL_BENCHMARK_20261003.md)

## Small LP End-to-End Baseline — 2026-10-03

### Scope and safeguards

This is a read-only solver baseline of the existing Release executable. The only new files are this report and its JSON companion. No solver, website, QP, MILP, tests, or benchmark inputs were changed; no build, commit, or push was performed. The worktree was already dirty at start (including existing QP/UI modifications); baseline Git HEAD was `365209b88701d889d74e7e44a5ec20524782dbcf`. Preserve those pre-existing changes.

### Dataset selection

The repository contains duplicated Netlib/Mittelmann copies of benchmark files. The smallest parsable MPS by raw dimensions is `small.mps` (2 columns, 2 rows, 4 nonzeros, 321 bytes), but it is an artificial toy and is excluded by the instruction to use a real LP. AFIRO is the smallest representative real LP found; AFIRO2 has the same dimensions but a larger 3,594-byte file. The AFIRO file in the Netlib directory is 3,244 bytes. Netlib identifies its LP data directory as a set of MPS test problems and lists AFIRO there ([Netlib LP data](https://www.netlib.org/lp/data/)).

| Candidate (unique model) | Columns | Rows | Matrix NNZ | File bytes |
|---|---:|---:|---:|---:|
| AFIRO | 32 | 27 | 83 | 3,244 |
| AFIRO2 | 32 | 27 | 83 | 3,594 |
| ADLITTLE | 97 | 56 | 383 | 16,500 |
| STOCFOR1 | 111 | 117 | 447 | 16,674 |
| SCAGR7 | 140 | 129 | 420 | 20,701 |
| BOEING2 | 143 | 185 | 1,283 | 48,147 |
| AGG | 163 | 488 | 2,410 | 99,348 |
| BANDM | 472 | 305 | 2,494 | 91,165 |
| SCAGR25 | 500 | 471 | 1,554 | 75,242 |
| STOCFOR2 | 2,031 | 2,157 | 8,343 | 329,484 |
| PILOT | 3,652 | 1,441 | 43,167 | 1,421,970 |

AFIRO and AFIRO2 are duplicate benchmark names in more than one repository folder; candidate rows above list each distinct name once. Dataset: `benchmarks/netlib/small/afiro.mps`, MPS, minimize, SHA-256 `fd3562804ff19382a9cd8bcb22ec81bffd24a4143a2290783831d8c64516a24b`.

### Original model, validation, and classification

| Property | Value |
|---|---:|
| Name / format / sense | AFIRO / MPS / minimize |
| Rows / columns / matrix nonzeros | 27 / 32 / 83 |
| Matrix density | 9.6065% |
| Continuous / integer variables | 32 / 0 |
| Variable bounds | 0 free; 32 lower-only (default lower 0); 0 upper-only; 0 double-bounded; 0 fixed |
| Constraint types | 8 equality; 19 `<=`; 0 `>=`; 0 ranged |
| Objective constant / nonzero objective coefficients | 0 / 5 |
| Objective coefficient range | -0.6 to 10 |
| Nonzero matrix coefficient absolute range | 0.107 to 2.429 |
| Finite RHS absolute range | 0 to 500 |
| Finite bound magnitudes | all finite bounds are 0 |

Both the native CLI parser and the repository MPS parser accepted the input. The parse showed no non-finite numeric values, duplicate column-row entries, invalid bounds, or unknown row references. The CLI does not print an explicit `Problem Type: LP` line for the LP path; the parsed model has no integer variables or quadratic terms, and the source classifies it as LP. Default method is revised simplex. This run explicitly requested `revised-simplex` and `--backend cpu`; the CLI's backend reason correctly says it was an explicit override. CPU was used; CUDA was not used. No GPU acceleration is involved in this LP route.

### Build and solver configuration

- Executable: `cpp_solver/build-route-cpu/sovereign_presolve_cli.exe` (479,232 bytes; timestamp 2026-10-03 13:21 local)
- CMake cache: Release, Ninja generator, MSVC compiler path using toolset 14.29.30133; Release flags `/O2 /Ob2 /DNDEBUG`.
- No rebuild was performed, so the measured binary is the existing binary and this report does not claim a fresh reproducible build from current source.
- Reproduction build command (not run for this baseline): `cmake --build cpp_solver/build-route-cpu --target sovereign_presolve_cli`.
- Method: Revised Simplex; CPU; presolve on; iteration limit 10,000; no LP time limit; feasibility and optimality tolerances 1e-8.
- Threads, exact pivot/refactorization policy, and phase-specific pivot counts are not exposed by this CLI. It is a fresh process with no LP warm start.
- Standardization scaling is enabled: one row max-norm pass followed by one column max-norm pass. Scale-factor ranges and post-scaling coefficient ranges are not instrumented.
- Dense memory estimate: 81,400 bytes, under the 268,435,456-byte budget; memory guard not triggered. Peak resident process memory was not measured.

### Presolve and standardization

| Model measure | Original | Presolved | Change |
|---|---:|---:|---:|
| Rows | 27 | 25 | -2 (-7.407%) |
| Columns | 32 | 32 | 0 (0%) |
| Matrix nonzeros | 83 | 81 | -2 (-2.410%) |

Measured presolve operations: 2 singleton reductions, 2 bound tightenings, 0 fixed variables, 0 substitutions, 0 eliminated variables, and 0 redundant rows. Two passes ran. The CLI does not expose empty-row/column attempted counts, identities of rows removed by singleton reductions, equality/inequality totals after presolve, presolved-space objective separately, objective-offset history, or postsolve stack sizes. No variables were eliminated/fixed/substituted, so postsolve reconstructed the original 32-variable solution without variable-value reversals. The objective constant is zero, so no objective offset is apparent in the input.

Presolve reduced the model's row count, but the solver's standardized tableau still had 35 rows, 32 columns, and 117 nonzeros. The no-presolve run had the same standardized dimensions and nonzero count. This happens because standardization expands each equality into two inequalities and adds rows for finite upper bounds; presolve tightened two upper bounds while removing two singleton rows. Thus the presolve counts do not translate into a smaller standardized dense tableau on this instance.

### Ten-run native benchmark

Each sample launches a fresh CLI process. `Solve time` is the CLI's solver-only timing after standardization; `Solve pipeline` includes standardization plus solver. Wall time is measured outside the executable with Python `perf_counter_ns`. First-run example stage readings: parse 0.7804 ms, presolve 0.1228 ms, standardization 0.0684 ms, solver 0.1264 ms, postsolve 0.0008 ms, verification 0.0020 ms. Sum of those separately timed stages: about 1.1008 ms (classification and CLI output are not separately timed). Process wall time includes process startup and output capture, so it is a distinct scope.

| Run | Parse ms | Presolve ms | Standardize ms | Solver ms | Pipeline ms | Postsolve ms | Verify ms | CLI wall ms | Iterations | Status |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 0.7804 | 0.1228 | 0.0684 | 0.1264 | 0.1948 | 0.0008 | 0.0020 | 25.4605 | 16 | OPTIMAL |
| 2 | 0.8120 | 0.1274 | 0.0697 | 0.1246 | 0.1943 | 0.0009 | 0.0025 | 15.9947 | 16 | OPTIMAL |
| 3 | 0.6991 | 0.1443 | 0.0719 | 0.1279 | 0.1998 | 0.0007 | 0.0021 | 15.3950 | 16 | OPTIMAL |
| 4 | 1.5370 | 0.1646 | 0.0988 | 0.1756 | 0.2744 | 0.0013 | 0.0037 | 19.0226 | 16 | OPTIMAL |
| 5 | 0.7840 | 0.1279 | 0.0712 | 0.1510 | 0.2222 | 0.0009 | 0.0021 | 16.4435 | 16 | OPTIMAL |
| 6 | 1.2377 | 0.1568 | 0.0816 | 0.1278 | 0.2094 | 0.0008 | 0.0022 | 17.0808 | 16 | OPTIMAL |
| 7 | 0.7416 | 0.1208 | 0.0706 | 0.1222 | 0.1928 | 0.0008 | 0.0021 | 16.8591 | 16 | OPTIMAL |
| 8 | 0.8444 | 0.1645 | 0.1021 | 0.1774 | 0.2795 | 0.0013 | 0.0032 | 16.3734 | 16 | OPTIMAL |
| 9 | 0.7472 | 0.1085 | 0.0744 | 0.1240 | 0.1984 | 0.0009 | 0.0023 | 17.1722 | 16 | OPTIMAL |
| 10 | 0.7218 | 0.1219 | 0.0679 | 0.1396 | 0.2075 | 0.0010 | 0.0023 | 15.1545 | 16 | OPTIMAL |

| Timing (10 runs) | Min ms | Median ms | Mean ms | Max ms | Sample std dev ms |
|---|---:|---:|---:|---:|---:|
| Solver-only | 0.1222 | 0.1279 | 0.1397 | 0.1774 | 0.0213 |
| Fresh CLI wall | 15.1545 | 16.6513 | 17.4956 | 25.4605 | 2.9994 |

All 10 runs returned OPTIMAL, 16 iterations, and the same objective. Process launch dominates the end-to-end wall measurement for this tiny LP; it is roughly two orders of magnitude larger than the internal solve. The CLI wall number must not be presented as solver time.

### Original-space independent verification

For every native run, the 32 returned variable values were matched to the original MPS variable names. Independently recomputed `c^T x + constant` was `-464.75314285714285`, exactly matching the displayed objective to the printed precision. Maximum original-row violation was `5.684341886080802e-14`; maximum bound violation was 0. The CLI's own original-model feasibility verification was PASS. It does not expose LP dual feasibility, reduced-cost residual, or complementarity for this revised-simplex path; these are N/A, not zero.

### Reference solvers (same AFIRO MPS)

Installed in the existing Python environment: HiGHS 1.15.1 and Gurobi 13.0.3. CPLEX was not installed, GLPK and SCIP commands were not found. `clp` resolved to a PowerShell alias rather than an executable, so CLP was not benchmarked. No packages were installed. Both references used one thread and their default presolve/settings. Times below use 10 API calls; their optimize-call wall excludes process startup and Python interpreter startup, unlike the fresh native CLI wall time.

| Solver | Status | Objective | Difference from Sovereign | Presolved rows / cols / nnz | Method / iterations | Reported solve time | Median optimize-call wall | Median model-read wall | Fresh Python process wall |
|---|---|---:|---:|---|---|---:|---:|---:|---:|
| Sovereign | OPTIMAL | -464.75314285714285 | 0 | 25 / 32 / 81 | Revised simplex / 16 | median 0.1279 ms | N/A (native fresh process) | included in CLI wall | median 16.6513 ms fresh CLI |
| HiGHS 1.15.1 | kOptimal | -464.75314285714285 | 0 | N/A / N/A / N/A | automatic simplex / 6 | median 0.6264 ms | median 0.6449 ms | median 0.3587 ms | median 330.0122 ms fresh Python process |
| Gurobi 13.0.3 | OPTIMAL | -464.75314285714285 | 0 | 6 / 9 / 26 | automatic / 2 | 0.0000 ms (timer resolution) | median 0.3346 ms | median 0.4199 ms | median 184.5286 ms fresh Python process |

HiGHS reported presolve status `kReduced`, but its Python API returned an empty presolved LP after the solve, so no dimensions are claimed. Gurobi's `presolve()` returned 6 rows, 9 columns, 26 nonzeros. The presolvers demonstrably produce different reduced models; counts alone do not prove equivalence of their internal representations. The iteration counts are not directly comparable because the methods and presolve reductions differ. Gurobi's time rounded to zero at its reported timer resolution; use API wall for this tiny instance. Gurobi ran under its restricted/non-production license, which permits this small model.

These scopes are not a fair speed contest. Sovereign is a native executable; reference fresh-process measurements also start Python and import solver packages (median 330.0 ms HiGHS, 184.5 ms Gurobi), while in-process API timings exclude that overhead. Solver-reported values are each library's own clocks. The measured evidence only says all three gave the same optimum; it does not support a winner claim.

### Presolve-off ablation (one run)

| Setting | Standardized rows / cols / nnz | Iterations | Solver ms | Pipeline ms | Objective | Verification |
|---|---:|---:|---:|---:|---:|---|
| Presolve on (representative run) | 35 / 32 / 117 | 16 | 0.1264 | 0.1948 | -464.75314285714285 | PASS |
| Presolve off (single run) | 35 / 32 / 117 | 16 | 0.1336 | 0.2071 | -464.75314285714285 | PASS |

The single no-presolve run is too small/noisy to claim a timing benefit. It confirms the standardized dimensions and iteration count are unchanged. There is no safe CLI option to disable scaling, so scaling's effect cannot be isolated in this baseline.

### Machine

Windows NT 10.0.26200.0; AMD64 Family 25 Model 68 Stepping 1 (AuthenticAMD); 16 logical processors; 16,989,736,960 bytes installed RAM. GPU not used.

### Primary summary

| Metric | Value |
|---|---|
| Dataset / suite / SHA-256 / format | AFIRO / Netlib / `fd3562804ff19382a9cd8bcb22ec81bffd24a4143a2290783831d8c64516a24b` / MPS |
| Original rows / columns / nnz / density | 27 / 32 / 83 / 9.6065% |
| Presolved rows / columns / nnz | 25 / 32 / 81 |
| Removed rows / columns / nnz | 2 / 0 / 2 |
| Row / column / NNZ reduction | 7.407% / 0% / 2.410% |
| Presolve / scaling / solver / postsolve / verification time | run 1: 0.1228 / 0.0684 / 0.1264 / 0.0008 / 0.0020 ms |
| Sum of separately timed internal stages | run 1: 1.1004 ms |
| CLI wall | median 16.6513 ms (10 fresh processes) |
| Method / backend / iterations | Revised Simplex / CPU / 16 |
| Status | OPTIMAL |
| Solver objective / independently recomputed objective / difference | -464.75314285714285 / -464.75314285714285 / 0 |
| Max primal violation / max bound violation | 5.684341886080802e-14 / 0 |
| Dual residual / complementarity | N/A / N/A |
| Verification | PASS |

### Findings and next bottleneck (no optimization performed)

1. Sovereign solves AFIRO correctly in these 10 runs; its original-model verification passes.
2. Presolve removes 2/27 rows and 2/83 matrix entries, but not columns. Standardized solver dimensions remain unchanged.
3. Among separately instrumented internal stages, parse takes around 0.7–1.5 ms in these samples; solver-only is about 0.12–0.18 ms. Fresh process wall time (median 16.65 ms) dominates tiny-model end-to-end time.
4. The one-run presolve-off ablation does not establish a speed benefit. It also has the same standardized dimensions and iterations.
5. Scaling is on, but its benefit cannot be measured without a supported off switch and separate controlled repetitions.
6. All three objective values match exactly to displayed precision. Gurobi's presolve reduces the model further by its reported dimensions than Sovereign's; HiGHS reported reductions but its exact dimensions were unavailable through the API used.
7. The next investigation for this tiny benchmark should be process/startup overhead and measurement methodology, followed by solver scaling/algorithm quality on substantially larger sparse models. This report does not start that work.

#### Telemetry gaps

CLI output does not expose phase-I/phase-II splits, pivot counts for dense simplex, detailed scaling factors, presolve attempted counts/row identities/history sizes, remaining presolved row categories, exact presolved HiGHS dimensions, dual/KKT residuals for revised simplex, or peak RSS. These are marked unavailable in the companion JSON rather than fabricated.

## Appendix 5: Milp Baseline 20261003

Source document: [benchmarks/milp_baseline_20261003.md](benchmarks/milp_baseline_20261003.md)

## MILP Baseline — 2026-10-03

Baseline captured from the checked-in solver before MILP source changes. The original baseline was built with the Visual Studio 2019 toolchain in CMake `Debug` mode (the Ninja `--config Release` flag does not switch configurations). That fact was discovered after the first after-change runs; the optimized `Release` comparison is therefore reported separately below.

### Build and tests

- `sovereign_presolve_cli` and `sovereign_milp_branch_and_bound_tests`: baseline build succeeded.
- `ctest --test-dir cpp_solver/build -R milp_branch_and_bound_tests --output-on-failure`: passed, 13.67 s.

### Measured baseline

All timed-out processes below were terminated by the benchmark harness. They did not return a solver status, incumbent, or verification result. No solver duration is inferred for those runs.

| Model | Size | Stage/result | Measured time | Nodes / LP work | Status / verification |
|---|---:|---|---:|---|---|
| `examples/milp_relaxation.json` | 2 vars, 1 row, 2 nnz | Full MILP solve | 3.69 ms solver; 70.41 ms process | 7 nodes created/processed, 7 LP solves, 8 LP iterations | OPTIMAL / PASS; objective -3 |
| `benchmarks/miplib/small/flugpl.mps` | 18 vars, 18 rows, 46 nnz (17 vars/16 rows after presolve) | Root LP | 16 iterations; 66.45 ms process including parse/presolve | 10 fractional integer vars | OPTIMAL LP relaxation; objective 1,167,185.7256 |
| `flugpl.mps` | same | B&B cap 100 nodes | 640.45 ms solver | 100 processed, 161 created, 259 pruned; 100 LP solves, 1,918 LP iterations | NODE_LIMIT; no incumbent |
| `flugpl.mps` | same | B&B cap 1,000 nodes | 3,373.14 ms solver | 1,000 processed, 1,483 created, 259 pruned; 1,000 LP solves, 19,622 LP iterations | NODE_LIMIT; no incumbent |
| `flugpl.mps` | same | B&B cap 3,000 nodes | 11,843.72 ms solver | 3,000 processed, 4,119 created, 941 pruned; 3,000 LP solves, 57,994 LP iterations | NODE_LIMIT; no incumbent |
| `flugpl.mps` | same | Default B&B; external 20 s cap | 20,025.39 ms process | Did not return | Externally terminated; no incumbent/status |
| `flugpl.mps` | same | Default B&B (10,000-node cap) | 53,545.59 ms solver; 53,617.47 ms process | 10,000 processed, 11,481 created, 4,260 pruned; 10,000 LP solves, 186,317 LP iterations | NODE_LIMIT; verified incumbent objective 1,201,500; dual bound 1,199,322.2222; absolute gap 2,177.7778 (0.1813% relative) |
| `datasets/milp/small/timtab1.mps` | 397 vars, 171 rows, 829 nnz (384 vars after presolve) | Default B&B; external 15 s cap | 15,091.81 ms process | Did not return | Externally terminated; no incumbent/status |
| `datasets/milp/medium/traininstance2.mps` | 12,890 vars, 15,603 rows, 41,531 nnz (12,878 vars after presolve) | Root LP only; external 20 s cap | 20,043.75 ms process, after 2,218.44 ms parsing + 902.82 ms presolve | LP result not returned | Externally terminated; root LP status unavailable |
| `datasets/milp/large/supportcase6.mps` | 130,052 vars, 771 rows, 584,976 nnz | Parse/analyze only | 18,946.78 ms process | Full MILP solve not started | Parsed successfully; solve not measured |

| Reference solver | Model | Measured solve | Result |
|---|---|---:|---|
| HiGHS 1.15.1 (comparison only; not part of the Sovereign solve path) | `flugpl.mps` | 235.86 ms including MPS read | OPTIMAL, objective 1,201,500, 89 nodes, zero gap |

### Bottleneck evidence

- The two-variable smoke MILP solves correctly in milliseconds, so the basic B&B path can terminate on a trivial instance.
- FLUGPL's root LP is modest (16 iterations), but the default run is dominated by search: it takes 53.55 s to process 10,000 nodes and 186,317 LP iterations. A verified incumbent is eventually found, matching HiGHS' objective, but the solver stops at NODE_LIMIT with a 0.1813% gap instead of proving optimality.
- The FLUGPL run with a 100-node cap takes 640 ms total solver time, including root work and Feasibility Pump; the 1,000- and 3,000-node runs take 3.37 s and 11.84 s. Search cost grows with the number of node LP solves. At the 3,000-node point no incumbent has been found yet.
- Medium `traininstance2` has a separate severe LP-relaxation bottleneck: root relaxation alone exceeded 20 s after parse and presolve. More B&B changes alone cannot address this.
- Large `supportcase6` spends about 19 s just parsing before any solver-stage output. This baseline does not establish its solve performance.
- Code inspection corroborates the measurements: each branch copies a complete `Model` twice; every child LP rebuilds a relaxed `Model`; open nodes use repeated vector scans/erasures; Feasibility Pump solves a second root LP and may run 50 projection LPs; pseudocost histories are scored but never updated; and the native B&B has no wall-clock deadline.

### After first MILP changes

| Model | Result | Time and work |
|---|---|---|
| FLUGPL | OPTIMAL, objective 1,201,500, Verification PASS | 16,591.78 ms solver; 9,819 nodes; 9,819 LP solves; 190,579 LP iterations; zero gap |
| `flugpl.mps`, 3,000-node diagnostic | NODE_LIMIT, no incumbent yet | 5,772.41 ms solver vs 11,843.72 ms baseline at the same node cap |
| `traininstance2.mps`, 5,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 2,110 ms solver time; 1,946 ms root LP, 8 LP iterations; 3,163 ms parse+presolve |
| `traininstance2.mps`, 20,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 17,044.68 ms solver time; 16,874.70 ms root LP, 97 iterations; 3,163 ms parse+presolve; no B&B nodes |

The first after-change FLUGPL Debug solve improved from 53,545.59 ms / NODE_LIMIT / 0.1813% gap to 16,591.78 ms / OPTIMAL / zero gap, a 3.23x measured speedup within that initial measurement. It matches HiGHS' objective and passes original-model verification. This improvement comes from eliminating the duplicate root relaxation, capping the repeated Feasibility Pump projections, tracking observed pseudocosts, using a priority queue, and replacing per-open-node full models with shared branch-bound deltas and a single reusable relaxed base model.

Stage telemetry shows FLUGPL remains LP-bound: after the changes, node LPs consume about 14.3 seconds of the 16.5-second B&B stage. Warm-start attempts remain zero because the revised-simplex path rebuilds its basis and current branch-bound changes can alter standardized row structure. The implementation does not claim warm starts.

The medium `traininstance2` instance remains difficult. Its root LP alone consumes 16.87 seconds at the 20-second full-run limit in Debug mode. HiGHS 1.15.1 also reaches its 20-second limit on this model, returning an incumbent objective of 79,720 with about 99.96% relative gap; Sovereign returns no incumbent within that same total budget. This is an identified remaining gap, not a claimed fix.

### Optimized Release validation

After correcting the build configuration with `-DCMAKE_BUILD_TYPE=Release`, the same code and model were remeasured. These are not directly comparable to the original Debug baseline as a solver-code-only speedup, but they represent the runtime when built with compiler optimizations enabled.

| Model | Result | Optimized Release measurement |
|---|---|---|
| FLUGPL | OPTIMAL, objective 1,201,500, Verification PASS | 1,507.18 ms solver; 9,819 nodes / LPs; 190,579 LP iterations; zero gap. Earlier Debug run of the same changed code took 16,549.99 ms (about 11.0x slower). |
| `traininstance2.mps`, 5,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 5,107.98 ms process; 4,652.50 ms solver after 384.86 ms preparation; root LP 4,627.78 ms / 317 iterations; no B&B nodes. |
| `timtab1.mps`, 2,000 ms total budget, before heuristic guard | TIME_LIMIT_NO_INCUMBENT | 4,238.14 ms process; Feasibility Pump took 2,907.40 ms for one projection LP after the root LP. |
| `timtab1.mps`, 2,000 ms total budget, with heuristic guard | TIME_LIMIT_NO_INCUMBENT | 2,621 ms process; 2,514 ms solver; root LP 1,108 ms; two B&B nodes / LPs, 400 LP iterations; no incumbent. The optional pump was skipped. One child LP exceeded the remaining budget. |
| `supportcase6.mps`, 5,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 5,387 ms process; MPS parse 2,471 ms, presolve 453 ms, MILP preparation 2,924 ms, root LP 1,937 ms / 15 iterations. The run returned structured timeout status rather than spending the full ~19 seconds parsing before any result. |

Release resolves the severe Debug-build distortion on the small reference MIP. The medium MIP is now limited mainly by its root relaxation, and no feasible integer incumbent is found in five seconds. A short-budget guard avoids starting the optional Feasibility Pump when less than one second remains. The overall time limit is still cooperative: the `timtab1` run exceeded its two-second request by about 0.6 seconds while finishing a child LP, so it is not a strict wall-clock cap.

### Limitations of this baseline

The original Debug baseline does not provide stage-level B&B timings. Cooperative solver deadlines can be exceeded by an in-progress LP operation (shown by `timtab1` in the Release validation). The FLUGPL competitor comparison uses an external solver and is informational; no equivalent competitor run is recorded for the medium or large model in this report.

### Small-MILP optimization and ablation — 2026-10-03

Scope was restricted to the same small FLUGPL model: 18 original variables, 11 general-integer variables, 0 binaries, 18 rows, 46 nonzeros, 85.8% sparsity, minimization. After presolve it has 17 active variables and 16 active constraints. Measurements use the MSVC Release executable (`/O2 /Ob2 /DNDEBUG`). Each setting was run twice, sequentially. Solver times below are internal elapsed milliseconds; HTTP/API timing is measured separately.

| Configuration | Solver ms (runs; mean) | Node-LP ms (mean) | Node-LP standardization ms (mean) | Nodes | LP solves | LP iterations | Warm attempts / accepted / fallback | Result |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Reusable LP structure, cold B&B | 381.30, 357.85 (369.58) | 270.23 | 16.75 | 7,143 | 7,143 | 139,029 | 0 / 0 / 0 | OPTIMAL, objective 1,201,500, PASS |
| Reusable LP structure, basis warm B&B | 682.56, 676.63 (679.60) | 544.38 | 22.16 | 9,819 | 9,819 | 50,865 | 9,818 / 8,452 / 1,366 | OPTIMAL, objective 1,201,500, PASS |

Warm basis reuse reduced total LP iterations by 63.4%, but the solve averaged 84% slower and processed 2,676 more nodes. Reusing a basis vector still rebuilds and factors the child tableau, so fewer pivots did not pay for that work on this small model. The website therefore uses the measured faster cold path by default; warm reoptimization remains available with the CLI `--warm-start 1` flag for further experiments. Failed warm starts fall back to a cold solve.

The measured pre-optimization Release run took 1,265.45 ms, with 9,819 nodes, 9,819 LP solves, and 190,579 iterations. The default cold result now averages 369.58 ms: a 70.8% reduction (3.42x speedup) against that recorded run. The earlier profile attributed 1,116.64 ms (88.2%) to node LP work, 225.21 ms to its repeated standardization, and only 0.34 ms to the root LP.

The main measured improvement was to prepare the standardized LP matrix once and update only the child RHS values. Node standardization fell from about 156 ms per run in the prior cold ablation to a 16.75 ms mean. The dense simplex also no longer refactors the same final basis solely to recover basic values; it reads the current tableau values and retains the existing LP and original-MILP verification checks. The resulting solver time is below the 500 ms target for both recorded Release runs.

The actual running website was exercised through `/api/analyze` and `/api/solve`: OPTIMAL, objective 1,201,500, Verification PASS, CPU backend, 352.66 ms solver time, 398.68 ms HTTP request time, 7,143 nodes, and 139,029 LP iterations. The health endpoint confirmed that it invoked the Release CPU executable. CUDA is detectable on this machine, but the MILP branch-and-bound and its node LP solves do not use the GPU.

The standalone feasibility pump was tested at caps of 6, 12, 20, and 40 iterations. It found no incumbent and stopped after 14 projections at the larger caps due to repeated points. Root-only strong branching with 20 probe LPs reached the 10,000-node limit without an incumbent and was reverted. Neither was retained as a default optimization.

No verifiable source or timing scope was available for the separately reported 16.9 ms comparison, so no parity claim is made. The earlier report's HiGHS comparison is a separate diagnostic measurement and was not used by the application solve path.

### Small-MILP follow-up: profiling and parent dictionary reuse

The 270 ms node-LP baseline was profiled before changing simplex behavior. Three diagnostic Release runs returned OPTIMAL, objective 1,201,500, and Verification PASS. Because the per-iteration clocks perturb timing, these numbers are only a phase profile, not a final timing comparison.

| Node-LP phase (sum across 7,143 LPs) | Diagnostic mean | Approx. share of 336 ms measured node-LP total |
|---|---:|---:|
| RHS standardization | 18.45 ms | 5.5% |
| Tableau/setup allocations and initialization | 62.09 ms | 18.5% |
| Reduced-cost pricing scans | 41.49 ms | 12.3% |
| Ratio tests | 35.77 ms | 10.6% |
| Simplex pivots | 75.66 ms | 22.5% |
| Solution recovery, verification, and cut extraction | about 8.3 ms | 2.5% |
| Other LP work and profiler overhead | about 94 ms | 28.0% |

The remaining share includes work outside the measured blocks, including objective-row setup, phase-I cleanup, and frequent timer overhead in the diagnostic build. Therefore, phase percentages are approximate and not additive evidence of exact CPU attribution.

An experimental true parent-to-child warm path now retains the parent dense simplex dictionary and basis, applies the child RHS delta through the stored slack columns, and reoptimizes with dual simplex. It avoids Gaussian basis reconstruction, but it did **not** outperform the cold default on FLUGPL. The table below supersedes earlier one- and two-run warm comparisons for these two final configurations. Five runs per configuration were sequential, with profiling timers disabled in the pivot loop.

| Configuration | Solver ms (five runs; mean) | Node LP ms (mean) | Nodes / LP solves | LP iterations | Warm starts accepted | Result |
|---|---:|---:|---:|---:|---:|---|
| Cold, with inherited-bound pruning | 361.86, 362.00, 377.46, 430.70, 437.98 (394.00) | 288.14 | 7,003 / 7,003 | 136,573 | 0 | OPTIMAL / 1,201,500 / PASS |
| Parent dictionary + dual reoptimization | 565.62, 506.59, 464.42, 462.30, 488.40 (497.47) | 361.42 | 8,547 / 8,547 | 22,566 | 7,951; 595 cold fallbacks | OPTIMAL / 1,201,500 / PASS |

The parent dictionary path reduced LP iterations by 83.5%, but was 26.3% slower overall, processed 22.1% more nodes, and first found an integer incumbent at node 6,086 instead of node 4,870. It remains opt-in through `--warm-start 1`; the website and CLI default stay cold. This is a concrete example of pivot-count savings failing to translate into wall-clock savings because tree shape and per-node setup matter.

The B&B now prunes queued children using their inherited parent bound before model materialization and an LP solve. In the cold FLUGPL run this skipped 140 LPs (1.96% of the previous 7,143), reducing LP iterations by 2,456. It did not change the optimal objective or verification. A generic nearest-integer root rounding attempt was also measured; FLUGPL's candidate failed full feasibility verification and was rejected, so it did not find an earlier incumbent. The first valid incumbent is still late in the search. Root cuts were not generated on this instance. Strong branching remains reverted because the earlier measured probe run did not help.

The small-MILP generalization checks were: `examples/milp_relaxation.json` (OPTIMAL, objective -3, PASS in 0.207 ms) and `datasets/milp/small/timtab1.mps` (3 s requested limit; root LP took 985.62 ms / 599 iterations, then the solver returned TIME_LIMIT_NO_INCUMBENT with no node LPs). Thus FLUGPL is correct and below 500 ms, but `timtab1` remains unsolved within the tested budget; no broad small-MILP speedup is established.

The live website `/api/analyze` + `/api/solve` path was exercised against the running Release CPU executable. FLUGPL returned OPTIMAL, objective 1,201,500, PASS; solver time was 362.12 ms, backend request time 399.48 ms, and the API reported 7,003 processed nodes / LP solves, 136,573 iterations, and CPU execution. CUDA is available on this machine, but this MILP path remains CPU-only.

#### Pseudocost reliability ablation

The previous default waited for two observed branch outcomes in each direction before using a variable's pseudocost estimate. Changing that reliability threshold to one observation per direction (no probe LPs or strong branching) consistently changed the FLUGPL search tree and improved the cold path:

| Configuration | Solver ms (five runs; mean) | Node LP ms (mean) | Nodes / LP solves | LP iterations | First incumbent node | Result |
|---|---:|---:|---:|---:|---:|---|
| Cold + inherited-bound pruning + pseudocost threshold 1 | 261.45, 319.45, 287.11, 283.90, 299.11 (290.20) | 210.28 | 5,367 / 5,367 | 105,334 | 3,504 | OPTIMAL / 1,201,500 / PASS |
| Same, with parent dictionary warm starts enabled | 320.85, 344.13, 331.00, 339.01, 331.58 (333.31) | 249.04 | 5,367 / 5,367 | 20,185 | 3,504 | OPTIMAL / 1,201,500 / PASS |

Using one observation reduced the cold configuration's solver mean by 26.3%, nodes/LP solves by 23.4%, and LP iterations by 22.9% relative to the threshold-2 cold run above. Under the improved branching policy, parent dictionary reuse reduced iterations by 80.8% but was still 14.9% slower; it remains opt-in. The website default is cold.

After this policy change, the live website/API was run again against the rebuilt Release executable: OPTIMAL, objective 1,201,500, Verification PASS, CPU backend, 283.29 ms solver time and 319.30 ms backend request time, with 5,367 processed nodes/LP solves and 105,334 LP iterations.

## Appendix 6: Qp Small Baseline And Fix 20261003

Source document: [benchmarks/QP_SMALL_BASELINE_AND_FIX_20261003.md](benchmarks/QP_SMALL_BASELINE_AND_FIX_20261003.md)

## Small QP baseline and optimization

### Scope and target

This phase covers only the repository's smallest representative QP, `QPLIB_9002.qplib` (in `datasets/qp/small`). The instance has 2,890 variables, 1,649 equality constraints, 9,690 linear constraint nonzeros, and a diagonal Hessian with 2,890 nonzeros. Its objective is convex, minimization, and has no linear terms or objective offset. The UI labels this instance MEDIUM under its solver-size policy even though it is in the `small` QP dataset directory.

QPLIB defines the quadratic objective as `1/2 x^T Q x + b^T x + q`; the parser and solver now preserve its objective offset `q`. Sources: [QPLIB format documentation](https://qplib.zib.de/doc.html), [QPLIB_9002 instance](https://qplib.zib.de/QPLIB_9002.html).

### Release baseline

The solver and tests were built using the repository's MSVC Release build (`/O2 /Ob2 /DNDEBUG`). Before the algorithm change, five direct CLI runs limited to one Newton iteration gave a median solver time of **9,394.54 ms** (range **9,296.75–9,539.37 ms**). Profiling showed **9,265.66 ms** in the linear solve. The implementation formed and factored a dense Schur matrix for 1,649 equality rows. That cubic dense factorization, rather than parsing or postsolve, was the dominant cost.

### Change and comparable timing

The QP solver now builds Schur products from sparse row/column data and solves larger Schur systems with preconditioned conjugate gradients (PCG), avoiding the dense `p × p` factorization. Diagonal Hessian convexity is checked directly instead of allocating a dense `n × n` matrix. The dense path remains for small Schur systems. The QP Auto route uses CPU because this PCG path has no CUDA implementation and no measured GPU benefit for this instance.

For the same one-Newton-iteration CLI scope, five Release runs had a median of **6.14 ms** (range **5.68–8.53 ms**), about **1,530× faster** than the dense baseline for that like-for-like iteration. The full solve converged in **43 Newton iterations** (43 linear-system solves; 7,059 accumulated PCG iterations). Five full solves had a median of **392.92 ms** (mean **393.99 ms**, range **380.81–405.43 ms**).

The full solve returned objective **5,698,097,498.146622**, status **OPTIMAL**, and original-model KKT verification **PASS**. Residuals were primal **7.59e-9**, stationarity/dual **2.43e-16**, and complementarity **4.69e-16**. These are measured solver timings, not an artificially reduced display value.

### Website validation

The updated Python backend and actual website were exercised on a local Release CPU build. The QPLIB_9002 solve returned `OPTIMAL`, `Verification: PASS`, and the objective above. The website showed **396.2 ms** solver time, CPU backend, 43 Newton iterations, 43 linear solves, and 7,059 PCG iterations. Its end-to-end backend measurement was **1,574.1 ms**, which includes parsing, model preparation, presolve, solver, and verification work; browser rendering and network time are excluded. The website also displayed per-stage QP timing telemetry.

### Reference and limitations

An independent objective comparison was unavailable in this environment: the installed Gurobi restricted license rejected the 2,890-variable model as exceeding its size limit, and the attempted HiGHS QP run did not complete successfully. Therefore no independent objective-match claim is made. Objective convention was checked against QPLIB's specification, and objective-offset behavior has parser and C++ regression coverage.

The current QP path still supports diagonal Hessians only, convex minimization, and bounded variables; it does not yet provide general off-diagonal sparse Hessians, free variables, robust infeasibility certificates, or medium/large QP validation. This work does not claim production-grade general QP coverage or GPU acceleration.

### Native CLI acceptance run

The first direct CLI attempt exposed that C++ input dispatch did not recognize QPLIB and silently treated it as the simple TXT format. A native QPLIB reader was added for the supported diagonal-Hessian/linear-constraint subset; it preserves objective offsets, maps QPLIB infinity sentinels, translates ranged rows, and rejects unsupported off-diagonal Hessian entries. The generic CLI now reports the QP Hessian nonzero count and omits thousands of primal values by default (`--print-primal` displays them explicitly). Website files remain in the repository as an optional adapter; they are not part of the C++ executable's dependency path.

Final direct command: `cpp_solver/build-route-cpu/sovereign_presolve_cli.exe --input datasets/qp/small/QPLIB_9002.qplib --method qp --backend cpu`. Exact executable: `C:\Users\Apurva\Downloads\Gpu-main\Gpu-main\cpp_solver\build-route-cpu\sovereign_presolve_cli.exe`. The actual dataset parsed natively as 2,890 variables, 1,649 constraints, 9,690 linear nonzeros, and 2,890 diagonal Hessian nonzeros. Five sequential Release runs returned OPTIMAL and Verification PASS, with 43 Newton iterations each. Solver times: min **373.608 ms**, median **381.475 ms**, mean **381.278 ms**, max **387.918 ms**. Separate full process wall times (including startup, parsing, and captured output) were 548.712, 484.087, 469.944, 458.078, and 459.676 ms (median **469.944 ms**). The final objective and KKT residuals match the values recorded above.

Release configuration was MSVC + Ninja, `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG`. All 10 CTest C++ targets and all 59 Python tests passed. CLI smoke runs also solved `examples/afiro.mps` with revised simplex (`OPTIMAL`, verification PASS) and `examples/milp.txt` with branch-and-bound (`OPTIMAL`, verification PASS). The exact build invocation used the Visual Studio 2019 `VsDevCmd.bat -arch=x64` environment followed by the bundled Ninja executable targeting `sovereign_presolve_cli` and `sovereign_ipm_tests`.

## Appendix 7: Lp Audit

Source document: [benchmarks/LP_AUDIT.md](benchmarks/LP_AUDIT.md)

## LP Audit — Phase 1 baseline

Scope: C++ LP solve path, its dashboard dispatch/fallback, and benchmark telemetry. MILP/QP algorithm changes are out of scope. This is a baseline and architecture audit, not a performance-improvement claim.

### Reproduce

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

### Pipeline findings

The production LP request parses and classifies the MPS in `main.cpp`, runs the C++ presolver, then calls one chosen LP class, reconstructs with `postsolve`, and checks the original rows and bounds. The web Auto path selects Revised Simplex for every continuous LP. On a qualifying solver failure, the web layer can run Dual Simplex as another attempt. Separately, the C++ process retries the original model only when a presolved result reports OPTIMAL but fails original-space feasibility verification. The CLI exposes only a yes/no presolve-fallback flag; it does not explain why or list all attempts.

Observed structural risks in the current implementation:

- The C++ presolver allows ten passes. Each pass scans singleton rows, may process only one two-term equality substitution, loops over all rows for each fixed variable, removes rows, and rebuilds active-index mappings. That makes repeated model scans and substitutions a direct candidate for medium/large presolve cost.
- The current LP Auto method is Revised Simplex regardless of actual LP structure. `LPSolver` internally decides dense versus sparse Revised Simplex with a size/density heuristic. Dual Simplex and Mehrotra IPM call dense `standardize()` directly and build dense matrices without using that guard.
- Dense standardization materializes `vector<vector<double>>`, expands equalities into two inequalities, adds finite upper bounds as rows, and can duplicate free variables. The Revised Simplex dense route then allocates a tableau and standard matrix. Mehrotra IPM forms a dense Schur/KKT matrix. Large LPs can therefore enter dense code through manually selected methods.
- The CPU PDHG path builds a sparse matrix, but the baseline 500-iteration runs did not meet all three residual tolerances. The CUDA PDHG implementation uploads its sparse matrix once, but periodically copies iterates back and performs residual checks on the CPU. The current production Auto policy does not select CUDA without measured same-model evidence.
- The standard benchmark folder has small/medium Netlib cases and Pilot, but no genuine million-scale sparse LP. A million-scale MILP ingestion smoke test is not evidence for million-scale LP support.

### Initial measurements

The baseline is intentionally bounded and reports status with timing. These runs do not all reach an optimum:

| Instance | Original rows × columns | Original NNZ | Presolve result | Standardized shape (instrumented) | Result/status |
|---|---:|---:|---|---:|---|
| AFIRO | 27 × 32 | 83 | 2 active rows removed; 2 passes | 35 × 32, 117 NNZ | OPTIMAL, original verification PASS |
| Bandm | 305 × 472 | 2,494 | 10 passes; active model 248 rows × 415 variables, 2,014 NNZ | 496 × 415, 4,028 NNZ | NUMERICAL_FAILURE, verification FAIL |
| Scagr25 | 471 × 500 | 1,554 | 10 passes; active model 337 rows × 489 variables, 1,395 NNZ | 748 × 489, 2,816 NNZ | ITERATION_LIMIT at 500/phase cap |
| Pilot | 1,441 × 3,652 | 43,167 | 7 passes; active model 1,427 rows × 3,441 variables, 41,064 NNZ | 2,696 × 3,441, 45,600 NNZ | NUMERICAL_FAILURE: sparse LU pivot failure |

In the three-repeat baseline, the existing executable returned NUMERICAL_FAILURE for Bandm and Pilot on every run, and ITERATION_LIMIT for Scagr25. The same-model presolve toggle also showed presolve enabled was slower on these bounded cases: Bandm medians were 9.25 ms presolve + 272.70 ms solver pipeline versus 0.31 ms + 84.83 ms without presolve; Scagr25 was 9.83 ms + 170.86 ms versus 0.25 ms + 145.79 ms. These are failed/iteration-limited runs, not optimal-solution speed comparisons. Use the JSON raw data and repeat the same pinned binaries before drawing performance conclusions.

Presolve pass telemetry from the instrumented run showed diminishing returns. For Bandm, pass 1 reduced the combined active variable/row/entry count by 13.24%, pass 2 by 3.14%, and the final six substitutions each reduced it by about 0.15%. Scagr25 reduced 10.22% in pass 1; subsequent passes were each below 0.3%. This supports testing a reduction-versus-time stop policy, while the exact policy should be benchmarked after it is implemented.

### Phase 1 changes

- Added per-pass presolve duration, active variables/rows/stored entries before and after, reductions by kind, and combined model-size reduction percentage.
- Pass-shape counters update from reductions instead of rescanning all rows at the end of every pass. Inactive rows are excluded from active-row presolve transforms and infeasibility checks.
- Added original NNZ, standardized rows/columns/NNZ, standardization time, and a separate optimization time in the CLI output. “Solve pipeline time” retains the full call duration.
- Added parsers to the Python benchmark and web API timing contracts for the new values. CSV output serializes nested profile data as JSON.
- Added a repeatable before/after LP audit harness and committed the raw three-run profile at `benchmarks/lp_audit_baseline.json`.

### Phase 2 changes

- C++ now performs exactly one LP algorithm attempt per process. If an optimal presolved candidate fails original-model verification, it is reported as `NUMERICAL_FAILURE`; the CLI explains that the automatic full-model retry was skipped. The measured timing and iteration count therefore cannot silently include a second algorithm run.
- Web Auto can launch at most one explicit Dual Simplex fallback. It records method, backend, status, original-model verification, total time, per-stage times, iterations, and the reason for each attempt. The automatic request shares one wall-clock budget across both attempts.
- Fallback preserves the same presolve setting. The previous behavior that silently enabled presolve on the retry is removed.
- Dual Simplex fallback is skipped if a conservative estimate for the dense standardized matrix/tableau/basis workspaces exceeds 256 MiB. The skip is returned in the attempt history. This is a fallback-specific guard; manually choosing dense algorithms is still an open memory-safety item.
- The Auto result panel now shows each attempt and its measured time/iterations, including why a fallback was skipped.

The Phase 2 tests cover attempt history, preserved presolve configuration, and dense-fallback memory eligibility. No solver speedup claim is made from this change. Presolve adaptation, comprehensive guards for manually selected dense algorithms, and structure-based algorithm dispatch remain open.

### Phase 3 changes and measurement

- Fixed-variable elimination now builds a variable-to-row adjacency index once for each elimination batch and touches only rows containing each fixed variable. This replaces the prior fixed-variable × all-rows scan.
- LP presolve stops after diminishing returns on models with at least 2,000 combined variables, rows, and stored entries. The stop condition uses both benefit and cost: stop if the reduction falls below the larger of 0.05% or 2.5% of the best reduction so far, or if reduction per millisecond falls below 2.5% of the best pass. There is still a hard 10-pass safety limit. Non-LP presolve keeps its existing pass behavior.
- Large LP Auto runs receive a pass-boundary time budget equal to 5% of the request limit, bounded from 25 ms to 2 seconds. A pass is not interrupted mid-operation; the profile below shows why this is only a between-pass budget.
- CLI/API/benchmark output includes the presolve stop reason and configured budget. `scripts/lp_audit_benchmark.py` accepts `--presolve-time-ms` for controlled budget tests.

The Phase 3 profile compares commit `df48c6f` (Phase 2) with the current implementation using MSVC x64 Release builds on the same Windows 11 machine. Both used the same four local Netlib models, 3 repetitions, presolve on, 500 iterations per simplex phase, and an 8-second process timeout. Raw samples are in `benchmarks/lp_audit_phase3.json`.

| Model | Phase 2 presolve ms / passes | Phase 3 presolve ms / passes | Phase 2 result | Phase 3 result |
|---|---:|---:|---|---|
| AFIRO | 0.135 / 2 | 0.117 / 2 | OPTIMAL, verification PASS | OPTIMAL, verification PASS |
| Bandm | 9.952 / 10 | 5.334 / 5 | NUMERICAL_FAILURE, verification FAIL | ITERATION_LIMIT, no candidate |
| Scagr25 | 8.709 / 10 | 4.487 / 4 | ITERATION_LIMIT, no candidate | ITERATION_LIMIT, no candidate |
| Pilot | 121.947 / 7 | 45.670 / 2 | NUMERICAL_FAILURE, no candidate | NUMERICAL_FAILURE, no candidate |

The Pilot presolve stage measured 62.5% lower (121.947 ms to 45.670 ms) with five fewer passes and six fewer total recorded reductions; the first pass still removes 204 fixed variables. Bandm and Scagr25 presolve stages measured 46% and 48% lower, respectively, after five and four passes instead of ten. AFIRO remained verified optimal with effectively unchanged presolve time. These are presolve-stage comparisons, not a claim that LP solving is fixed: Bandm/Pilot still do not solve, and the Bandm status changed from numerical failure to the bounded iteration limit. Solver-time differences across changed reduced models must not be interpreted as same-problem algorithm speedups. The 500-iteration cap is diagnostic.

The budget check runs only between passes. A deliberately 1 ms budget on Pilot stopped after its first pass at 32.8 ms, so a single expensive pass can exceed the budget. A finer-grained interrupt/checkpoint inside transformations remains future work.

### Phase 4 changes

- Added a conservative dense peak-workspace estimator for standardized form, tableau, standard matrix, basis, and vector/row storage. The shared limit is 256 MiB.
- Revised Simplex routes to its existing sparse implementation before dense standardization if the estimate exceeds the limit. Dual Simplex and Mehrotra IPM return `UNSUPPORTED` with the estimate and budget instead of allocating dense matrices. Direct dense `standardize()` also rejects over-budget requests.
- CLI, benchmark CSV/JSON, and Auto attempt telemetry now include the estimate, budget, and whether the guard triggered. The Auto attempt panel reports memory routing and the solver message.
- Added a 4,000-row × 4,000-column sparse regression fixture with 4,000 nonzeros. Its estimated dense peak is about 977.9 MiB; tests confirm Revised Simplex selects sparse, Dual Simplex/IPM refuse, and direct dense standardization throws before allocation.

The memory guard is about safety, not a performance result. The synthetic fixture only checks route/guard behavior and does not establish general 4,000 × 4,000 solve performance. CUDA free-memory admission checks and full large-model memory profiling remain open.

### Phase 5 algorithm-selection measurements and policy

An MSVC x64 Release executable was used for three runs each on AFIRO, Bandm, Scagr25, and Pilot, with presolve on, a 2,000-iteration cap, and an 8-second process timeout. Raw Revised Simplex and PDHG measurements are in `lp_audit_phase5_revised.json` and `lp_audit_phase5_pdhg.json`; Dual Simplex measurements are in `lp_audit_phase5_dual.json`.

| Model | Revised Simplex median solve ms / status | PDHG median solve ms / status | Dual Simplex median solve ms / status |
|---|---:|---:|---:|
| AFIRO | 0.226 / OPTIMAL, verification PASS | 1.828 / ITERATION_LIMIT | 27.853 / OPTIMAL |
| Bandm | 250.245 / NUMERICAL_FAILURE | 59.504 / ITERATION_LIMIT | 3,131.772 / NUMERICAL_FAILURE |
| Scagr25 | 707.882 / NUMERICAL_FAILURE | 51.724 / ITERATION_LIMIT | TIMEOUT at 8 s |
| Pilot | 507.420 / NUMERICAL_FAILURE | 688.654 / ITERATION_LIMIT | UNSUPPORTED by dense-memory guard |

PDHG was faster on Bandm and Scagr25 in this bounded diagnostic, but did not produce a verified solution on any of the four instances; it was slower on Pilot and AFIRO. Dual Simplex was substantially slower on AFIRO and Bandm, timed out on Scagr25, and was correctly refused for Pilot. These results do not support a broad PDHG or Dual Simplex default. The pilot Auto policy therefore retains Revised Simplex for ordinary small/medium structures and routes only very large sparse LPs (at least 100,000 columns, 10,000 rows, 1,000,000 entries, and 99.5% sparsity) to sparse PDHG as the scale-oriented algorithm. It disables a second full-solve fallback on that route. That threshold is a transparent routing policy, not an empirically established speed crossover; status and original-model verification remain authoritative. CPU is retained because no paired, verified CPU/CUDA crossover has been established.

No million-scale input is present in the checked-in benchmark corpus. Phase 5 validates the dispatch decision with synthetic analysis metadata only; it does not claim million-scale parse, memory, convergence, or solve performance.

### Phase 6 sparse Revised Simplex profile and experiment

The sparse Revised Simplex path now reports pricing, basis solves, Devex updates, factorization/refactor counts, ratio-test time, lexicographic anti-cycling work, and pivots. Timing labels are internal stage measurements; lexicographic time is a subset of ratio-test work and must not be added to it. The first profile is `lp_audit_phase6_profile.json`; it used a MinGW GCC `-O2 -DNDEBUG` build, the same machine and inputs for three repetitions, presolve on, 2,000 iterations, and an 8-second process timeout. The final experiment is `lp_audit_phase6_adaptive_ties8k.json` under the same procedure.

| Model | Profile solve ms / status | Profile hotspot | Adaptive-tie solve ms / status | Iterations, profile → adaptive |
|---|---:|---|---:|---:|
| AFIRO | below timer resolution / OPTIMAL, PASS | Dense path | below timer resolution / OPTIMAL, PASS | 16 → 16 |
| Bandm | 228.0 / NUMERICAL_FAILURE, verification FAIL | Dense path | 214.0 / NUMERICAL_FAILURE, verification FAIL | 926 → 838 |
| Scagr25 | 794.0 / NUMERICAL_FAILURE, verification FAIL | Factorization 414.2 ms; 75 refactors; 710 lexicographic solves | 748.2 / NUMERICAL_FAILURE, verification FAIL | 1,093 → 1,106 |
| Pilot | 1,438.4 / NUMERICAL_FAILURE, no candidate | Lexicographic tie solves 442.3 ms (3,632 solves); factorization 461.9 ms | 430.8 / NUMERICAL_FAILURE, no candidate | 585 → 315 |

The sparse Phase I ratio test now estimates tie work as `tie count × basis dimension`. When this exceeds an 8,192 row-work budget, it uses a stable row tie-break and tracks basis hashes; if a basis repeats, the remaining Phase I pivots switch to Bland entering/leaving order. Smaller tie sets retain the original exact lexicographic rule. On Pilot the lexicographic work fell to 5.3 ms and 84 solves. On Scagr25 it remained 21.5 ms and 624 solves. The sparse LU detail is now appended to, rather than overwritten by, the dense-memory routing explanation; Pilot reports a failed pivot at basis position 1,497 with magnitude 0.000000.

These are time-to-failure changes: Bandm, Scagr25, and Pilot still fail and produce no verified optimum. Do not describe them as solved-model speedups. The experiment proves the expensive Pilot lexicographic work was a material part of the diagnostic path, but its early sparse-LU pivot failure remains the blocking correctness issue.

Rejected experiments were also measured: always using Bland’s rule raised Scagr25 from 794 ms to 2,361 ms and Pilot from 1,438 ms to 2,965 ms, with both still failing; changing the fixed refactor interval from 15 to 30 caused Pilot to fail after 60 pivots and did not establish a safe general improvement. Both settings were discarded. Pricing and factorization improvements still need to preserve verified solutions, and Devex pricing/factorization remain open work.

### Phase 7 CPU PDHG measurements

The CPU PDHG path now tests ergodic averages of primal/dual iterates every 25 steps, stores the recovered average only when all residual checks pass, builds a CSC view once for cache-friendly `A^T y`, and releases the temporary triplet buffer after CSR construction. It now reports scaled primal, dual, and complementarity residuals. A result remains `OPTIMAL` only when those residuals, the absolute original-model feasibility tolerance, finite-value checks, and scaled original-model feasibility all pass. Non-PDHG CLI residual fields are `N/A`, not fabricated zeroes.

For an A/B check, the Phase 6 commit's PDHG header was compiled as a baseline and compared with the current CPU PDHG implementation using MinGW GCC 6.3 `-O2 -DNDEBUG`, 3 repetitions, presolve enabled, 50,000 iterations, and an 8-second per-process timeout. Raw data: `lp_audit_phase7_pdhg_50k_baseline.json` and `lp_audit_phase7_pdhg_50k_average_csc.json`.

| Model | Baseline solve ms / status | Average + CSC solve ms / status | Current median residuals (primal / dual / complementarity) |
|---|---:|---:|---:|
| AFIRO | 38.521 / ITERATION_LIMIT | 37.256 / ITERATION_LIMIT | 3.14e-6 / 8.29e-2 / 1.11e-1 |
| Bandm | 1,100.701 / ITERATION_LIMIT | 989.869 / ITERATION_LIMIT | 1.99e-4 / 5.33e-1 / 3.66e-4 |
| Scagr25 | 849.643 / ITERATION_LIMIT | 813.007 / ITERATION_LIMIT | 1.70e-4 / 1.64e-2 / 3.34e-1 |
| Pilot | TIMEOUT at 8 s | TIMEOUT at 8 s | No result |

The 3–10% timing differences are not usable solve speedups: no benchmark reached the required convergence and verification criteria. The observed iterates do not justify routing these Netlib cases to PDHG as a way to obtain answers. The extra CSC storage and iterate averages therefore remain a CPU-path implementation experiment, not evidence that PDHG is ready for production large LPs.

A small unit fixture (`x + y >= 1`, minimize `x + y`) also ran 100,000 iterations. It recovered a feasible objective-1 point but still reported `ITERATION_LIMIT`; the test asserts it cannot be labeled `OPTIMAL` without the required residuals. The original absolute-feasibility check now also gates convergence, preventing an internally scaled residual from masking a failed original-model check. Remaining CPU PDHG work includes better diagonal equilibration/acceleration and a converged medium/large benchmark; no such improvement is claimed here.

### Phase 8 CUDA PDHG audit

Built the current source with MSVC 19.29, CUDA 13.1, and a GeForce RTX 3050 Laptop GPU (4 GiB, compute capability 8.6). `scripts/lp_audit_benchmark.py` now accepts `--backend cpu|cuda|auto`, records the selected backend, and covers all 13 checked-in Netlib cases. Both backend runs use one CUDA-enabled executable, so they share the same compiler, solver revision, and preprocessing. Each 2,000-iteration result is the median of three runs; presolve is enabled. Raw data is in `lp_audit_phase8_cpu_2k.json` and `lp_audit_phase8_cuda_2k_average.json`.

| Model | Rows × columns / NNZ | CPU solve ms | CUDA solve ms | CPU status | CUDA status |
|---|---:|---:|---:|---|---|
| AFIRO | 27 × 32 / 83 | 1.70 | 267.51 | ITERATION_LIMIT | ITERATION_LIMIT |
| Afiro2 | 27 × 32 / 83 | 1.80 | 263.51 | ITERATION_LIMIT | ITERATION_LIMIT |
| Adlittle | 56 × 97 / 383 | 7.24 | 269.25 | ITERATION_LIMIT | ITERATION_LIMIT |
| Agg | 488 × 163 / 2,410 | 30.18 | 257.82 | ITERATION_LIMIT | ITERATION_LIMIT |
| Bandm | 305 × 472 / 2,494 | 46.95 | 261.50 | ITERATION_LIMIT | ITERATION_LIMIT |
| Boeing1 | 440 × 384 / 3,819 | 48.58 | 264.79 | ITERATION_LIMIT | ITERATION_LIMIT |
| Boeing2 | 185 × 143 / 1,283 | 18.39 | 265.06 | ITERATION_LIMIT | ITERATION_LIMIT |
| Scagr25 | 471 × 500 / 1,554 | 40.87 | 260.88 | ITERATION_LIMIT | ITERATION_LIMIT |
| Scagr7 | 129 × 140 / 420 | 10.48 | 264.97 | ITERATION_LIMIT | ITERATION_LIMIT |
| Pilot | 1,441 × 3,652 / 43,167 | 513.33 | 286.17 | ITERATION_LIMIT | ITERATION_LIMIT |
| Small | 2 × 2 / 4 | 0.14 | 289.18 | ITERATION_LIMIT | ITERATION_LIMIT |
| Stocfor1 | 117 × 111 / 447 | 8.71 | 272.65 | ITERATION_LIMIT | ITERATION_LIMIT |
| Stocfor2 | 2,157 × 2,031 / 8,343 | 169.55 | 276.60 | ITERATION_LIMIT | ITERATION_LIMIT |

All 13 cases, including Afiro2 (same dimensions as AFIRO), reached the iteration cap; none of these rows is a solve-speed comparison. At 2,000 iterations the CUDA path takes about 258–289 ms across the corpus, largely due to per-process CUDA setup/launch/synchronization costs, and only Pilot is faster than the CPU path. Explicit CPU therefore remains the correct Auto choice for these checked-in cases. These dimensions also do not represent the requested million-scale regime.

The CUDA path had two avoidable costs/consistency gaps. cuSPARSE dense-vector descriptors already retain the device pointers they were created with, so the four per-iteration `cusparseDnVecSetValues` calls were removed. CUDA also used to check only the latest iterate, while the CPU path checks ergodic averages. The CUDA update kernels now accumulate primal and dual sums without an extra kernel launch, residual checks use those averages, and a converged result returns the average. CUDA checks run every 25 iterations through iteration 500 and then every 250 iterations to reduce host round-trips; the configured final iteration is always checked.

At 20,000 iterations, repeated twice on Agg, Bandm, Pilot, and Stocfor2, all CPU and CUDA runs still ended at `ITERATION_LIMIT`. CUDA and CPU residuals match to numerical precision after the averaging change, confirming the same candidate/residual semantics. CUDA medians were 2,738 ms / 2,374 ms / 2,420 ms / 2,443 ms; CPU medians were 310 ms / 468 ms / 5,745 ms / 1,827 ms, respectively. Pilot is about 2.4× faster on CUDA in this capped diagnostic, while the other three cases remain slower. The lower-frequency check schedule reduced the pre-average CUDA time by roughly 8–17% in this sample, but that gain varied after averaging and is not a solved-instance speedup. Raw runs are in `lp_audit_phase8_cpu_20k.json`, `lp_audit_phase8_cuda_20k_average_sparse_checks.json`, and the intermediate checkpoint-frequency experiment files.

The current timing includes CUDA allocations, model upload, solver work, and synchronization inside the timed solve call. No backend threshold was changed: these results do not establish a reliable general crossover, and every larger benchmark still fails to converge within the test cap. The built-in Product mix LP also reached the 10,000-iteration limit on both CPU and CUDA with matching residuals (primal 0, dual 9.83e-4, complementarity 7.31e-4); the previous CUDA-only smoke result was not a CPU/GPU behavior difference. The checked-in corpus contains no million-scale sparse LP, so the large-workload routing policy remains unvalidated. The CUDA path only implements PDHG; Revised Simplex and Dual Simplex stay CPU-native. The CUDA-enabled build, PDHG unit test, and device-info check pass on the RTX 3050 host.

### Phase 9 PDHG step-weight experiment

The native PDHG implementation uses a fixed relative primal/dual step weight of 1. The current benchmark residuals showed an imbalance between primal and dual progress, so a diagnostic tested a fixed initial weight of `||c||₂ / ||b||₂` (clamped to `[1e-4, 1e4]`) while keeping the product of the diagonal primal and dual step sizes unchanged. The C++ result and CLI now expose the chosen weight so future experiments are observable.

The norm-ratio weight was rejected as a default. On 20,000-iteration CPU runs, all four cases still hit the cap. Pilot solve time changed from 5,745 ms to 5,223 ms, but its dual residual worsened from 0.869 to 0.999 and complementarity from 0.759 to 0.997. Bandm's dual residual worsened from 0.477 to 0.642. Stocfor2's dual residual improved from 0.469 to 0.083, but complementarity worsened from 0.112 to 0.765. Agg remained effectively unchanged. At 2,000 iterations, AFIRO, Small, Pilot, and Stocfor2 also remained `ITERATION_LIMIT`. Because the change trades progress among KKT conditions and does not consistently produce a verified solution, the solver keeps weight 1. Raw data is in `lp_audit_phase9_weight_cpu_20k.json` and `lp_audit_phase9_weight_cpu_2k.json`.

This result is consistent with the limits of changing only a fixed weight: practical large-scale PDHG research combines adaptive step sizes, restart logic, diagonal preconditioning, and presolve; the reference method is described in the [PDLP paper](https://arxiv.org/abs/2106.04756). This repository does not use that external solver or its code. The native solver still lacks a tested adaptive step/restart scheme, and the current experiments do not justify a claim that its PDHG path solves medium or large LPs.

### Phase 9 broader native LP-method profile

Ran the native Mehrotra IPM on AFIRO, Bandm, Scagr25, and Pilot, then the native Revised Simplex on the remaining checked-in Netlib cases (MSVC Release, CPU, presolve on, 3 repetitions, 2,000 iteration limit, 12-second per-run timeout). Raw runs are `lp_audit_phase9_ipm_cpu.json` and `lp_audit_phase9_revised_cpu.json`.

| Method / instance | Median solve time or limit | Result across 3 runs |
|---|---:|---|
| IPM / AFIRO | 978 ms | ITERATION_LIMIT |
| IPM / Bandm | 12 s process timeout | TIMEOUT |
| IPM / Scagr25 | 12 s process timeout | TIMEOUT |
| IPM / Pilot | 0.11 ms | UNSUPPORTED by dense memory guard |
| Revised / AFIRO | 0.22 ms | OPTIMAL |
| Revised / Agg | 17.42 ms | OPTIMAL |
| Revised / Bandm | 235 ms | NUMERICAL_FAILURE |
| Revised / Boeing1 | 301 ms | NUMERICAL_FAILURE |
| Revised / Boeing2 | 14.49 ms | OPTIMAL |
| Revised / Scagr25 | 563 ms | NUMERICAL_FAILURE |
| Revised / Pilot | 324 ms | NUMERICAL_FAILURE |
| Revised / Stocfor2 | 5,658 ms | ITERATION_LIMIT |

The IPM implementation forms a dense `p × p` normal-equation matrix with a nested row/column/nonzero accumulation every Newton direction, then solves it with the generic dense `LinearSystem`; that makes the algebraic work roughly proportional to `p² × n` before the dense factorization. Its current tolerance checks also provide no iteration-level residual output, so the 978 ms AFIRO result is time-to-limit, not time-to-solution. Pilot is refused before dense allocation. The Revised Simplex figures confirm that 5 of the 12 benchmark instances in this run do not return an optimum; Bandm, Boeing1, Scagr25, and Pilot fail numerically, while Stocfor2 exhausts the cap. The method profile therefore does not support “the solver is slow only because of CUDA”; the main issues are algorithm convergence, numerical reliability, and dense work in IPM.

Together with the earlier Dual Simplex results, the current in-house LP algorithms have these limits: Revised Simplex solves the easy sparse cases but fails several larger ones; Dual Simplex builds dense matrices, is slow or fails on modest models, and refuses larger ones; IPM's dense normal equations time out or hit memory admission limits; PDHG is the only sparse GPU-capable path, but the current fixed-step method did not converge on the medium/large Netlib tests under the audit caps. There is no verified large-instance CPU/GPU win because the checked-in corpus has no truly large sparse LP.

### Phase 10 presolve A/B and Auto policy correction

To isolate the Revised Simplex verification failures, ran the same MSVC Release dense Revised Simplex binary with presolve on and off on the same four Netlib models (3 repetitions, 2,000 iterations, 12-second timeout). Raw output: `lp_audit_phase10_dense_presolve_ab.json`.

| Model | Presolve on: median / status / original check | Presolve off: median / status / original check |
|---|---:|---:|
| Bandm | 233 ms / NUMERICAL_FAILURE / FAIL | 357 ms / OPTIMAL / PASS |
| Boeing1 | 283 ms / NUMERICAL_FAILURE / FAIL | 253 ms / OPTIMAL / PASS |
| Scagr25 | 549 ms / NUMERICAL_FAILURE / FAIL | 413 ms / OPTIMAL / PASS |
| Scagr7 | 9.8 ms / NUMERICAL_FAILURE / FAIL | 14.1 ms / OPTIMAL / PASS |

Each row had the same outcome in all three repetitions. This identifies a more consequential presolve regression than the earlier timing-only sweep: its reduced-model candidates were being rejected by original-space verification, whereas the same native method solved the untouched model. Presolve-on candidate objectives also differed from the verified no-presolve optima on Scagr25 and Scagr7. The no-presolve route is slightly slower on Bandm and Scagr7, but returns a verified optimum; time-to-failure is not an acceptable speed metric.

The web Auto policy already disabled presolve for the three medium cases Bandm, Boeing1, and Scagr25. It now also disables it for structurally similar 100–200 variable/row LPs with at least 97% sparsity, covering the measured Scagr7 case. MILP/QP policy and the LP method are unchanged. The direct CLI still lets users explicitly use or disable presolve.

An experimental broadened sparse routing rule was also rejected. Routing Bandm to sparse Revised Simplex did not change its presolved `NUMERICAL_FAILURE` result and increased median solve time from 233 ms to about 1,236 ms. No-presolve dense Revised Simplex returned `OPTIMAL` in about 357 ms. Raw path experiment: `lp_audit_phase10_sparse_route_trial.json`; matched sparse-route presolve data: `lp_audit_phase10_presolve_ab.json`. Pilot still fails under both presolve settings because sparse Phase I reports an auxiliary unbounded status; Stocfor2 does not finish within these caps. Those remain open correctness/performance failures.

## Appendix 8: Audit Report

Source document: [AUDIT_REPORT.md](AUDIT_REPORT.md)

## Sovereign solver prototype audit

**Audit date:** 2026-09-30\
**Scope:** current working tree, the executable selected by the web API, checked-in benchmark records, unit tests, and direct local benchmark runs.\
**Rule followed:** no solver or UI implementation was changed during this audit.

### Executive result

Sovereign is a genuine prototype solver with working verified paths for small LP and diagonal convex QP problems. The CUDA executable is real and the QP path genuinely calls cuSOLVER. It is **not ready to claim broad large-scale LP/MILP capability**. The most urgent gaps are the non-configurable MILP 10,000-node cap, the large-MPS C++ parsing path, false completion stages in the UI, incomplete telemetry, and benchmark/documentation claims that disagree with recorded evidence.

#### Runtime evidence collected in this audit

| Input / path | Result | Mathematical check |
|---|---|---|
| Netlib `afiro.mps`, Revised Simplex CPU | `OPTIMAL`, 18 iterations, objective `-464.75314285714285` | `Verification: PASS`, maximum feasibility error `1.42e-14` |
| Mittelmann `afiro.mps`, Revised Simplex CPU | `OPTIMAL`, 18 iterations, same objective | `Verification: PASS`, maximum feasibility error `1.42e-14` |
| Convex QP JSON, QP Newton/Barrier CUDA | `OPTIMAL`, 13 iterations, objective `2.6476763e-08` | `Verification: PASS`; primal and dual residuals `0`; complementarity `6.62e-09` |
| MIPLIB `flugpl.mps`, Branch-and-Bound CPU | `ITERATION_LIMIT`, 10,000 nodes processed, feasible incumbent `1201500` | `Verification: PASS`; remaining absolute gap `2225` |
| Uploaded `supportcase6.mps` (25.4 MB) | C++ process failed after preprocessing | Python analysis completed: 130,052 variables, 771 rows, 584,976 linear nonzeros; no candidate was returned |

The CUDA runtime reported one usable device: **NVIDIA GeForce RTX 3050 Laptop GPU**, compute capability 8.6, CUDA 13.1.

---

### A. FULLY WORKING

| Requirements | Finding | Evidence |
|---|---|---|
| 1 | Solving code is project-owned; no external optimization solver library is called by the C++ solve paths. | `cpp_solver/include/{lp,milp,qp}` and `cpp_solver/src/main.cpp` |
| 2–3 | LP and Revised Simplex work for the audited Netlib and Mittelmann AFIRO inputs. | Direct commands above; original-model verification passed. |
| 6 | The automatic MILP route invokes `BranchAndBound`, which solves LP relaxations. | `cpp_solver/src/main.cpp`, `cpp_solver/include/milp/BranchAndBound.hpp` |
| 9 | Best-bound node selection and pseudo-cost score bookkeeping are executed in the B&B loop. | `BranchAndBound.hpp` selects the best open bound and updates pseudo-costs after child solves. |
| 10–12 | Convex diagonal QP classification and a Newton/barrier KKT solve are called by the CLI. | `GeneralQP.hpp`, `QPIPM.hpp`; verified CUDA QP run. |
| 16 | MPS integer markers and binary bounds are parsed and classified. | Python tests pass; MIPLIB input routes to MILP. |
| 17 | Presolve performs bound tightening, fixed-variable elimination, equality substitution, redundant-row deletion and singleton reductions. | `Presolver.hpp`; AFIRO reported 4 reductions. |
| 24–25 | LP verification occurs on the original model after postsolve. | `main.cpp`: `postsolve(...)` then `checkPrimal(model, sol)`. |
| 28 | LP and QP do not convert an iteration limit into `OPTIMAL`. | Status mapping in `main.cpp`; direct MIPLIB result remained `ITERATION_LIMIT`. |
| 30–32 (LP/QP) | CLI `--max-iterations N`, including explicit `0 = unlimited`, is implemented for LP/QP. | `main.cpp`; UI exposes the setting. |
| 35–36 (LP) | Parse, presolve, solve, postsolve, and verification timings are independently measured; browser time is excluded. | LP CLI fields; direct AFIRO output. |
| 37–40 | A CUDA build exists, is selected by the website for `backend=cuda`, and runtime device status comes from that executable’s `--device-info`. | `webui/app.py`, `scripts/build_cuda.bat`, direct device-info output. |
| 43–47 | Automatic policy keeps LP simplex and MILP on CPU; CUDA is selected only for eligible medium/large QP. | `webui/solver_policy.py` |
| 50–52 | API analysis reports real LP/QP/MILP classification, dimensions, types, bounds, objective sense, and sparsity. Missing solver values remain null rather than becoming zero. | `analyze_model`, `_solution_variables` |
| 53, 55–59 | Variable Explorer uses returned values and original metadata; it filters, sorts, searches all loaded values, uses tolerance for at-bound, retains names, and paginates. | `webui/static/app.js`, `webui/app.py` |
| 60–64 | The matrix view is a real coefficient heatmap with 50×50 aggregation; it does not change the model. | `analyze_model`, `renderMatrix()` |
| 78–79 | CLI and independent Python benchmark runner remain available. | `cpp_solver/src/main.cpp`, `sovereign_solver/benchmark.py` |
| 84–85 | Automatic selection explains method/backend, and Expert mode exposes method, backend, presolve, time and iteration controls. | `solver_policy.py`, `app.js` |
| 86–87 | Current policy does not pretend every problem is GPU accelerated and does not replace working solver paths for presentation. | `solver_policy.py` |

---

### B. PARTIALLY WORKING

| Req. | Affected files | Reason | Severity | Smallest safe fix |
|---|---|---|---|---|
| 4 | `cpp_solver/include/lp/MehrotraIPM.hpp`, `main.cpp` | IPM exists and has unit coverage, but no audited real LP benchmark was verified with it. Its KKT solve is still dense. | MEDIUM | Add real LP IPM regression cases with residual thresholds and report method-specific capability. |
| 5 | `BranchAndBound.hpp`, `main.cpp` | MILP produces verified incumbents, but audited MIPLIB run did not prove optimality. | HIGH | Repair scalable node handling and pass policy limits to B&B. |
| 7 | `CuttingPlane.hpp`, `main.cpp`, `webui/app.py` | Gomory cuts exist and can be manually selected, but automatic B&B never integrates them. | MEDIUM | Either integrate root/node cuts with metrics or label the UI method as experimental. |
| 8 | `FeasibilityPump.hpp`, `BranchAndBound.hpp` | Feasibility Pump is manually selectable and is called only as a root incumbent heuristic. It is not separately surfaced as an automatic strategy or metric. | MEDIUM | Return heuristic attempts/outcome in `MILPResult` and render only produced values. |
| 13–15 | `sovereign_solver/qplib.py`, `mps.py`, `cpp_solver/include/model/Input.hpp` | Python QPLIB conversion works for tested standard QPLIB text and the prior QPLIB-9002 run; C++ directly accepts only JSON/MPS/TXT. MPS works for small files, but large MPS parsing is unsafe. | HIGH | Make one canonical parser/IR path for analysis and C++ solve, then add raw QPLIB and MIPLIB fixtures. |
| 18 | `main.cpp`, `webui/app.py` | Before/after variables, rows, and reduction count are real. Before/after nonzero counts are absent. | MEDIUM | Emit and parse original/reduced linear nonzeros. |
| 19–22 | `SparseMatrix.hpp`, `LPSolver.hpp`, `LinearSystem.hpp`, `Factorization.hpp` | CSR, sparse LU, eta updates, LU, Cholesky and dense KKT abstractions exist. Only `LPSolver` conditionally uses sparse simplex above an entry threshold; Dual Simplex/IPM still construct dense matrices. | HIGH | Route large LP/dual/IPM paths through sparse factorizations and add memory-bound tests. |
| 23 | `Tolerance.hpp`, LP/QP files | Tolerances, pivot tests, singularity checks, residual checks and QP scaling exist. Coverage against ill-conditioned real models is thin. | MEDIUM | Add numerical stress suite and report numerical failure causes. |
| 26 | `main.cpp`, `QPIPM.hpp`, `webui/app.py` | LP reports maximum primal feasibility; QP reports primal, dual and complementarity. Bound, integrality, objective consistency and a labelled KKT residual are not consistently exposed. | MEDIUM | Build one structured verification payload per class with `N/A` for unavailable quantities. |
| 27 | `main.cpp`, `app.py`, `app.js` | Most statuses exist, but a verification failure becomes `NUMERICAL_FAILURE` for LP and QP/MILP do not offer a distinct `VERIFICATION_FAILED` status. | HIGH | Add a canonical status enum and preserve it through parser/UI. |
| 29, 33–34 | `solver_policy.py`, `app.py` | Limits are centrally configured, but automatic small/medium/large time limits are all `0` (unlimited), not approximately 60/120/300 seconds. API and subprocess use the same value when nonzero. | HIGH | Define central 60/120/300 defaults, retain explicit user-selected zero as unlimited, and test it end to end. |
| 31 | `main.cpp`, `BranchAndBound.hpp`, `app.py` | Web API intentionally omits `--max-iterations` for MILP. B&B has a separate hardcoded 10,000-node constructor default. The direct MIPLIB command proved `--max-iterations 50000` did not affect it. | **CRITICAL** | Add `--max-nodes`/`--node-limit`, pass it from API policy, report the selected limit, and keep LP/QP iteration limits separate. |
| 41–42 | `CudaBackend.cu`, `QPIPM.hpp`, `MehrotraIPM.hpp` | cuBLAS, cuSPARSE and cuSOLVER wrappers are real. Audited QP path calls cuSOLVER dense solve; vector operations may use cuBLAS in IPM. cuSPARSE is implemented but not shown as active in an audited solver run. | MEDIUM | Add per-operation backend telemetry and tests that prove each used path. |
| 48–49 | `solver_policy.py`, `app.py` | Classification → selection → device check → backend happens. Selection is heuristic; there is no measured-benefit database or runtime calibration. | MEDIUM | Base CUDA eligibility on validated capability/size thresholds and publish measured pairs only. |
| 65–67 | `main.cpp`, `app.py`, `app.js` | CLI emits many MILP/QP/LP fields, but result UI shows only a subset: MILP omits bounds, LP solves and cuts; QP omits complementarity/KKT label and QP/MILP lack solve timing. | MEDIUM | Use structured solver JSON or fully parse/render every genuine field. |
| 68–75 | `benchmark.py`, `results/` | Dataset directory support and a runner exist. Recorded runs show only 1/10 verified Netlib, 0/1 MIPLIB and 6/7 QPLIB. There is no verified real MIPLIB optimal solve. | HIGH | Re-run a curated reproducible suite with fresh metadata, retain failures, and make the UI show only those records. |
| 80 | `app.py`, `app.js` | The intended pipeline exists conceptually, but API solve is synchronous and stage events are not streamed. | MEDIUM | Run jobs asynchronously and publish real stage transitions. |
| 81–83 | `app.js`, `app.py` | During a run, progress after solve is shown as pending. On any returned result `renderResult` marks every stage done, including postsolve/verify after timeout/failure. | **CRITICAL** | Return per-stage states from backend and render them exactly; mark skipped stages as skipped. |

---

### C. BROKEN / BUGS

| Req. | Affected files | Reason | Severity | Smallest safe fix |
|---|---|---|---|---|
| 15, 20, 74–77 | `cpp_solver/include/model/Input.hpp`, `webui/app.py` | The large `supportcase6.mps` was analyzed by Python in under 9 seconds, but the C++ process later failed after a very long parse phase. `parseMPS` builds each constraint by scanning every variable and does repeated name scans. | **CRITICAL** | Build a row-oriented sparse representation while reading COLUMNS, index variable names once, and reject unsupported dimensions with a precise status. |
| 31 | `BranchAndBound.hpp`, `main.cpp`, `app.py` | MILP has an invisible 10,000-node cap and ignores the visible iteration setting. | **CRITICAL** | See B/31. |
| 35, 65–67 | `main.cpp`, `parse_solver_output` | QP and MILP branches return before emitting solve/postsolve/verification timings. Result cards show `—` even for actual work. | HIGH | Use one timing wrapper and emit all timings for every solve class. |
| 52, 65 | `webui/static/app.js` | `number(value, 0)` renders unavailable MILP statistics as `0`, which looks like real data. | HIGH | Preserve null as `N/A` in every metric card. |
| 81–83 | `webui/static/app.js` | `renderResult` applies `done` to all stages after a failed, timed-out, or skipped solve. | **CRITICAL** | See B/81–83. |
| 93–94 | `BENCHMARKS.md`, `results/` | `BENCHMARKS.md` claims broad 2.8×–3.1× HiGHS speedups and multiple successful MIPLIB/GPU results. Current result summaries show no GPU pairs, no external reference comparisons, 1/10 verified Netlib, and 0/1 verified MIPLIB. | **CRITICAL** | Replace the document with generated current results or mark it historical/unverified and remove speed claims from demo material. |
| 93 | `webui/static/app.js` | The running bar is intentionally animated, not solver-progress telemetry. Its copy admits events are not streamed, but it can still look like measured progress. | MEDIUM | Label it indeterminate and show real elapsed time/stage only. |
| 93 | `webui/app.py` | `/api/analyze` returns up to 5,000 points plus metadata for every variable. For `supportcase6` that means 130,052 metadata objects, producing a very large response before solving. | HIGH | Return summary + heatmap buckets by default; paginate metadata separately only when Variable Explorer needs it. |

---

### D. NOT IMPLEMENTED

| Req. | Affected files | Reason | Severity | Smallest safe fix |
|---|---|---|---|---|
| 20–21 | Dual Simplex/IPM and core linear algebra | No production sparse basis update/factorization path is used by Dual Simplex/IPM; no million-scale evidence exists. | HIGH | Add sparse factorization integration before making large-model claims. |
| 26 | CLI/API verification schema | Consistent bound/integrality/objective/KKT reporting across LP, QP and MILP is absent. | MEDIUM | Define shared verification result schema. |
| 29 | `solver_policy.py` | Approximate 60/120/300 automatic time budgets are not configured. | HIGH | Configure and test policy limits. |
| 41–42 | CUDA telemetry | Kernel time, transfer time, memory consumption and exact operation attribution are not emitted. | MEDIUM | Add CUDA events and structured output. |
| 73 | Benchmarks / result records | No audited real benchmark MILP was proven optimal; the requirement for one verified LP, MILP and QP demonstration is incomplete. | HIGH | Select a small MIPLIB instance that terminates or present a verified feasible incumbent clearly as non-optimal. |
| 76–77 | README/demo claims | No million-variable demonstration or defensible scalability evidence. | HIGH | State this as future work only. |

---

### E. IMPLEMENTED BUT NOT CONNECTED TO WEBSITE

| Req. | Affected files | Reason | Severity | Smallest safe fix |
|---|---|---|---|---|
| 7 | `CuttingPlane.hpp`, `main.cpp`, `solver_policy.py` | Manual expert method only; no automatic integration. | MEDIUM | Integrate with policy or hide behind explicit experimental label. |
| 8 | `FeasibilityPump.hpp` | Root heuristic is called internally but its iterations, projections and result are not surfaced. | MEDIUM | Include heuristic metrics in `MILPResult` and UI. |
| 22 | `Factorization.hpp` | Cholesky implementation exists but was not traced into an active production QP/LP route. | LOW | Remove unused claim or integrate and test it. |
| 41–42 | `CudaBackend.cu` | cuBLAS/cuSPARSE wrappers are present but no UI shows which operation actually used which library. | MEDIUM | Return operation-level CUDA telemetry. |
| 65–67 | `main.cpp`, `app.js` | Several produced fields are discarded by `parse_solver_output` or never rendered. | MEDIUM | Prefer a machine-readable solver result. |

---

### F. GPU/CUDA STATUS

**Real working status:** CUDA is available and the website resolves `backend=cuda` to `cpp_solver/build-cuda/sovereign_presolve_cli.exe`. Runtime `--device-info` confirmed the RTX 3050 Laptop GPU and CUDA 13.1.

**What really runs on the GPU:** the QP KKT dense linear solve calls `cuda::Context::solveDense`, which uses cuSOLVER. CUDA source also implements cuBLAS AXPY/dot/GEMV and cuSPARSE CSR SpMV, with host-device copies on every call. The audited QP CUDA run was verified, so this is genuine CUDA execution, though it is not fully resident on device and has no measured speedup evidence.

**What remains CPU:** Revised Simplex, Dual Simplex, branch-and-bound control, presolve, parser, postsolve, verification, and automatic MILP. The policy states this correctly. No GPU speedup should be claimed from the current `results/` records because every recorded CPU/GPU pair count is zero.

---

### G. LP STATUS

Small real LP solving is working and verified. AFIRO passes both Netlib and Mittelmann source locations. Revised Simplex measures all stages and applies original-model verification after postsolve.

LP remains unsuitable for broad performance claims: the recorded Netlib summary has 1 verified success from 10 runs, 1 timeout and 6 other failures. Large LP handling has a conditional sparse revised-simplex path, but other LP methods still rely on dense matrices. The CPU binary reports `Build type: configuration unspecified`, so release compilation should be standardized before measuring performance.

---

### H. MILP STATUS

MILP is a real LP-relaxation branch-and-bound prototype with best-bound selection, pseudocost scoring, root feasibility pump, pruning, gap fields, and original-model feasibility/integrality verification. The direct `flugpl.mps` run produced a verified incumbent.

It is currently capped at 10,000 B&B nodes independently of the UI or `--max-iterations`, so the visible configuration is misleading. The recorded MIPLIB report has no verified successful instance. Large MIPLIB parsing/solve failures have insufficient error detail. Treat MILP as an experimental small-instance capability until node limits, parsing, and structured status reporting are corrected.

---

### I. QP/QPLIB STATUS

QP classification checks convexity and the CLI uses a Newton/barrier KKT solve. The CUDA QP test verified a solution. The previous QPLIB-9002 run correctly classified QP and preserved 2,890 variables, 1,649 constraints, and 12,580 total nonzeros before reaching a verified optimum in 22 iterations.

The supported QP envelope is narrower than "standard QPLIB" in general: the canonical JSON/C++ input supports diagonal quadratic terms, and `GeneralQP` rejects variables without a finite bound and unsupported nonconvex structure. Raw QPLIB is parsed in Python and converted to canonical JSON for the C++ executable. This should be documented as a supported subset and protected with official raw QPLIB regression fixtures.

---

### J. NUMERICAL ROBUSTNESS STATUS

Implemented protections include tolerance centralization, pivot/singularity thresholds, finite checks, Harris-style ratio protection in simplex, residual checks, original-model LP verification, QP convexity checks, and variable scaling in the general QP transform. The QP output reports primal/dual/complementarity values.

Missing evidence is more important than missing classes: no comprehensive ill-conditioned/degenerate benchmark battery validates these protections, and statuses are not normalized across LP/QP/MILP. The highest risk is dense memory and repeated factorization in large methods, not a single tolerance constant.

---

### K. BENCHMARK STATUS

The Python benchmark runner is sound in intent: it invokes the CLI, verifies an original-model primal, keeps unsupported/failure states, and keeps optional HiGHS comparisons clearly optional. Checked-in result files are recorded artifacts, not generated at page render time; `/api/benchmarks` reads them.

The data contradicts the promotional `BENCHMARKS.md` document. Current summaries state:

| Dataset | Recorded result |
|---|---|
| Netlib | 10 instances; 1 verified success; 1 timeout; 6 other failures |
| Mittelmann | Result files exist; no fresh audit run beyond AFIRO |
| MIPLIB | 1 instance; 0 verified successes |
| QPLIB | 7 instances; 6 verified successes; 1 unsupported |

No recorded comparison has a real CPU/GPU pair or a real optional external solver comparison. The website benchmark table faithfully displays the stored records, but the documentation must be corrected before a demo.

---

### L. UI/TELEMETRY ISSUES

1. The front-end makes every pipeline stage look complete after a result, even when solving failed or timed out. This violates truthful status reporting.
2. The live bar is indeterminate animation. It reports real elapsed wall time but no solver iterations or stage events.
3. QP and MILP omit several timing values because their CLI branches do not emit them.
4. Metric cards can turn unavailable values into `0`, which is indistinguishable from a measured zero.
5. The Variable Explorer is functionally correct but the analysis response sends every variable’s metadata. This is expensive for 130k variables and should be paginated server-side.
6. The heatmap is correct: it uses real matrix buckets, has an unavailable state, and is bounded at 50×50. Its API payload should drop legacy sampled points when the heatmap is the only consumer.

---

### M. TEST RESULTS

| Test | Result |
|---|---|
| Python `unittest discover -s tests -q` | **41/41 passed** (above prior 40-test baseline; no regression) |
| Compiled C++ tests | **7/7 passed**: presolve, cutting plane, LP relaxation, feasibility pump, NLA, IPM, LP numerical |
| Netlib AFIRO | Verified optimal LP |
| Mittelmann AFIRO | Verified optimal LP |
| Convex QP CUDA | Verified optimal QP |
| MIPLIB FLUGPL | Verified feasible incumbent; iteration limit before proof |
| Large supportcase6 MIPLIB | Analysis succeeded; C++ solver process failed; not a valid solve |
| CMake configure | Not run because `cmake` is not on this machine’s PATH. Prebuilt executables and their tests were run directly. |

The test suites are valuable but they did not prevent the large-MPS failure or expose the B&B configuration disconnect. Add those as integration tests.

---

### N. CRITICAL FIXES BEFORE DEMO

1. **Fix B&B control and status reporting** — add a node-limit argument, wire it through policy/API/CLI, and make the UI display `ITERATION_LIMIT`/`NODE_LIMIT` accurately.
2. **Fix large MPS parsing and failure diagnostics** — eliminate rows×variables reconstruction, preserve sparse rows while parsing, and return an explicit parse/solver failure reason.
3. **Make pipeline stages truthful** — backend returns per-stage states; UI uses `completed`, `running`, `failed`, or `skipped`, never blanket `done`.
4. **Remove or regenerate unsupported benchmark claims** — replace `BENCHMARKS.md` speed/MIPLIB assertions with the actual records currently presented by the site.
5. **Finish telemetry** — emit timing and available solver metrics in every solve branch; use `N/A` for unavailable values.
6. **Reduce API payload size for large models** — heatmap summary by default, server-paginated metadata/solution rows on demand.

---

### O. POST-DEMO ROADMAP

1. Establish a release build through CMake/Ninja or a documented MSVC build, then capture compiler flags and dependency versions in every benchmark.
2. Define one structured result protocol from C++ to API rather than parsing text logs.
3. Complete sparse factorization/update integration for large LP, Dual Simplex and IPM; set memory thresholds from measured data.
4. Expand QP support beyond the verified diagonal/bounded subset, with official QPLIB regression corpus and convexity checks.
5. Improve MILP with configurable node/time/gap limits, safe cut integration, heuristic telemetry, and a curated verified MIPLIB suite.
6. Add CUDA event/memory instrumentation, persist data on device where algorithms benefit, and publish CPU/GPU measurements only from matched verified runs.
7. Add integration tests for raw QPLIB upload, 25–128 MB MPS upload, cancellation, time limit, node limit, and UI stage truthfulness.

### Requirement disposition index

**Implemented/verified:** 1–3, 6, 9–12, 16–17, 24–25, 28, 30–32 for LP/QP, 37–40, 43–47, 50–64, 78–79, 84–87, 88–90.\
**Partial or unverified:** 4–5, 7–8, 13–15, 18–23, 26–27, 29, 31 for MILP, 33–36, 41–42, 48–49, 65–75, 80–83, 91–92.\
**Broken/incorrect:** 15/20 for large MPS, 31 for MILP configuration, 35/65–67 metrics, 52 unavailable-to-zero rendering, 81–83 stage truthfulness, and 93–94 benchmark documentation claims.\
**Not demonstrated:** 73, 76–77 and all broad performance/scalability claims.

## Appendix 9: Benchmarks

Source document: [BENCHMARKS.md](BENCHMARKS.md)

## Sovereign Optimization Solver — Comprehensive Benchmark & Timing Report

---

### 1. Executive Summary

This report documents the empirical evaluation and detailed per-test timing breakdown for the **Sovereign Optimization Solver**, a hybrid C++17 and Python optimization solver prototype designed for linear (LP), mixed-integer linear (MILP), and quadratic (QP) mathematical programming.

Evaluations were performed across four benchmark suites: **Netlib LP**, **MIPLIB 2017**, **QPLIB**, and **Mittelmann LP**, comparing against industry baseline **HiGHS 1.15.1** (`highspy`) and evaluating GPU acceleration on an **NVIDIA GeForce RTX 2050 Laptop GPU**.

#### High-Level Benchmark Summary

| Dataset Family | Evaluated | Verified Optimal / Feasible | Baseline (HiGHS) Comparison | Primary Methods Evaluated | Key Performance Findings |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **Netlib LP** | 10 | 5 Solved (3 Verified Optimal) | HiGHS 1.15.1 | Revised Simplex, Dual Simplex, CUDA IPM | **2.8x – 3.1x speedup** over HiGHS on small dense instances (`afiro`, `adlittle`). Sub-millisecond cold starts. |
| **MIPLIB 2017** | 10 | 5 Solved / Feasible | HiGHS 1.15.1 | Branch-and-Bound (`milp`), Feasibility Pump | Solves structured binary problems (`50v-10`, `air05`, `b1c1s1`, `app1-1`) to verified optimality at root node. |
| **QPLIB** | 7 | 5 Verified Optimal | Internal KKT Verification | Newton-Barrier Interior Point (`qp`) | 100% correct Hessian convexity classification (PD, PSD, Indefinite). Fast barrier convergence. |
| **Mittelmann LP** | 5 | 5 Solved (3 Verified Optimal) | HiGHS 1.15.1 | Revised Simplex, Dual Simplex | Consistent sub-millisecond execution matching published objective baselines. |

---

### 2. Hardware & Software Test Environment

| Component | Specification |
| :--- | :--- |
| **Processor (CPU)** | 12th Gen Intel(R) Core(TM) i5-12450H (8 Cores / 12 Threads @ 2.00 GHz base, up to 4.40 GHz Turbo) |
| **Host Memory** | 16.0 GB DDR4 RAM |
| **Graphics Processing Unit (GPU)** | NVIDIA GeForce RTX 2050 Laptop GPU (4.0 GB GDDR6, 2048 CUDA Cores, Compute Capability 8.6) |
| **NVIDIA Driver & CUDA** | Driver 592.82 (CUDA 13.1 / 13.4 Runtime Toolkit) |
| **Operating System** | Microsoft Windows 11 Home (x86_64) |
| **C++ Toolchain** | MinGW-w64 GCC 16.2.0 (MinGW UCRT, `-O3 -DNDEBUG`) / MSVC 19.44 + NVCC 13.4 |
| **Build Systems** | CMake 4.4.2 + Ninja 1.13.2 |
| **Python Environment** | Python 3.14.3 (`numpy 2.4.4`, `scipy 1.17.1`, `highspy 1.15.1`) |
| **Git Branch** | `devansh-gpu` on [Apurva1234-coder/Gpu](https://github.com/Apurva1234-coder/Gpu.git) |

---

### 3. Detailed Benchmark Timings & Per-Test Results

#### 3.1 Netlib Linear Programming (LP) Benchmark

Evaluated using Revised Simplex (`--method simplex`), Dual Simplex, and CUDA Interior Point Method (`--method ipm --backend cuda`).

| Instance Name | Constraints ($m$) | Variables ($n$) | Nonzeros ($nnz$) | Sovereign CPU Time (ms) | Sovereign GPU Time (ms) | HiGHS 1.15.1 Time (ms) | CPU Speedup vs HiGHS | Sovereign Objective | Reference Objective | Verification Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **`afiro.mps`** | 27 | 32 | 83 | **0.18 ms** | — | 0.53 ms | **2.98x** | `-464.753143` | `-464.753143` | **PASS (Exact)** |
| **`afiro2.mps`** | 27 | 32 | 83 | **0.17 ms** | — | 0.53 ms | **3.09x** | `-464.753143` | `-464.753143` | **PASS (Exact)** |
| **`adlittle.mps`** | 56 | 97 | 383 | **1.41 ms** | Time Limit | 3.96 ms | **2.81x** | `225494.9630` | `225494.9632` | **PASS ($\Delta \le 10^{-4}$)** |
| **`scagr7.mps`** | 95 | 140 | 420 | **1.21 ms** | **42.6 ms** (16 iters) | 0.66 ms | 0.54x | `0.000000` | `0.000000` | **PASS (Exact)** |
| **`scagr25.mps`** | 347 | 500 | 1,554 | **47.63 ms** | **51.8 ms** (15 iters) | 16.90 ms | 0.35x | `0.000000` | `0.000000` | **PASS (Exact)** |
| **`agg.mps`** | 488 | 163 | 2,410 | **4.86 ms** | **84.2 ms** (30 iters) | 1.33 ms | 0.27x | `0.000000` | `0.000000` | **PASS (Exact)** |
| **`boeing2.mps`** | 125 | 143 | 1,283 | 857.2 ms | Time Limit | 4.36 ms | — | Non-converged | `-376.315615` | Dense tableau limit |
| **`bandm.mps`** | 305 | 472 | 2,494 | 6,915.2 ms | Time Limit | 9.28 ms | — | Non-converged | `-158.628018` | Dense tableau limit |
| **`boeing1.mps`** | 351 | 384 | 3,819 | 7,265.6 ms | Time Limit | 9.79 ms | — | Non-converged | `-402.502439` | Dense tableau limit |
| **`pilot.mps`** | 1,441 | 3,652 | 43,167 | Timeout (>60s) | Time Limit | 862.8 ms | — | — | `-557.489729` | Dense $O(m^2 n)$ limit |

---

#### 3.2 MIPLIB 2017 Mixed-Integer Linear Programming Benchmark

Evaluated using the upgraded **Branch-and-Bound engine with Pseudo-Cost & Fractional Scoring + Root Feasibility Pump**. Verified against `miplib2017-v37.solu`.

| Instance Name | Variables (Binary) | Constraints | Branch Nodes | Sovereign MILP Time (ms) | HiGHS 1.15.1 Time (ms) | Sovereign Objective | Verified Reference | Status / Remarks |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`academictimetable`** | 512 (512 bin) | 128 | 1 | **16.3 ms** | 84.3 ms | `0.0000` | `0.0000` | **OPTIMAL ($5.17\times$ faster)** |
| **`50v-10.mps.gz`** | 50 (50 bin) | 10 | 1 | **229.3 ms** | 25.3 ms | `0.0000` | `0.0000` | **OPTIMAL (Verified)** |
| **`assign1-5-8.mps.gz`** | 64 (64 bin) | 32 | 124 | **820.2 ms** | 60,006.6 ms | `212.0000` | `212.0000` | **Feasible Incumbent Found** |
| **`b1c1s1.mps.gz`** | 250 (250 bin) | 100 | 1 | **924.3 ms** | 8.3 ms | `0.0000` | `0.0000` | **OPTIMAL (Verified)** |
| **`air05.mps.gz`** | 7,195 (7,195 bin) | 426 | 1 | **3,849.7 ms** | 14.4 ms | `0.0000` | `0.0000` | **OPTIMAL (Verified)** |
| **`app1-1.mps.gz`** | 417 (417 bin) | 300 | 1 | **15,493.9 ms** | 8.8 ms | `0.0000` | `0.0000` | **OPTIMAL (Verified)** |
| **`30n20b8.mps.gz`** | 30 (30 bin) | 20 | 14,800+ | Timeout (>60s) | 67.3 ms | — | Infeasible | Time Limit (Tree depth) |
| **`app1-2.mps.gz`** | 1,145 (1,145 bin) | 800 | — | Timeout (>60s) | 82.4 ms | — | `0.0000` | Time Limit |
| **`atlanta-ip.mps.gz`** | 1,200 (1,200 bin) | 600 | — | Timeout (>60s) | 146.6 ms | — | `0.0000` | Time Limit |
| **`bab2.mps.gz`** | 300 (300 bin) | 150 | — | Timeout (>60s) | 1,300.0 ms | — | `0.0000` | Time Limit |

---

#### 3.3 QPLIB Quadratic Programming Benchmark

Continuous quadratic programming with objective $\min \frac{1}{2} x^T Q x + c^T x$ subject to linear constraints and bounds.

| Instance Name | Problem Structure | Convexity Classification | Sovereign Solve Time (ms) | Computed Objective | KKT Residuals | Solver Decision |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`convex_qp.json`** | Diagonal ($Q > 0$) | **Strictly Convex (PD)** | **9.69 ms** | $2.00 \times 10^{-8}$ | $\|r_p\| \le 10^{-8}$ | **OPTIMAL** |
| **`lower_bound_qp.json`** | Diagonal ($Q > 0$) | **Strictly Convex (PD)** | **9.94 ms** | $-9.000000$ | $\|r_p\| = 0$ | **OPTIMAL** |
| **`psd_qp.json`** | Semidefinite ($Q \ge 0$) | **Convex (PSD)** | **9.88 ms** | $1.03 \times 10^{-8}$ | $\mu \le 5.2 \times 10^{-9}$ | **OPTIMAL** |
| **`general_ineq_qp.json`** | Diagonal ($Q > 0$) | **Strictly Convex (PD)** | **10.24 ms** | $-3.000000$ | $\|r_d\| \le 10^{-16}$ | **OPTIMAL** |
| **`upper_bound_qp.json`** | Diagonal ($Q > 0$) | **Strictly Convex (PD)** | **10.62 ms** | $-9.000000$ | $\|r_p\| = 0$ | **OPTIMAL** |
| **`mixed_qp.json`** | Mixed Bounds | **Strictly Convex (PD)** | **10.84 ms** | $-5.000000$ | Candidate Feasible | **Solved (within tolerance)** |
| **`nonconvex_qp.json`** | Indefinite ($Q_{11} < 0$) | **Non-Convex** | **10.29 ms** | N/A | Correct Rejection | **REJECTED (Non-Convex)** |

---

#### 3.4 Mittelmann LP Comparative Benchmark

| Instance Name | Problem Class | Dimensions ($m \times n$) | Sovereign Runtime (ms) | HiGHS Runtime (ms) | Speedup Factor | Objective Match |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`afiro.mps`** | LP | $27 \times 32$ | **0.20 ms** | 0.51 ms | **2.59x** | Exact Match (`-464.7531`) |
| **`afiro2.mps`** | LP | $27 \times 32$ | **0.19 ms** | 0.52 ms | **2.82x** | Exact Match (`-464.7531`) |
| **`adlittle.mps`** | LP | $56 \times 97$ | **1.40 ms** | 3.22 ms | **2.29x** | Match ($\Delta = 1.6 \times 10^{-4}$) |
| **`scagr7.mps`** | LP | $95 \times 140$ | **1.02 ms** | 0.55 ms | 0.54x | Exact Match (`0.0000`) |
| **`agg.mps`** | LP | $488 \times 163$ | **3.85 ms** | 1.22 ms | 0.32x | Exact Match (`0.0000`) |

---

### 4. GPU Acceleration & CUDA IPM Analysis

The solver was tested on the **NVIDIA GeForce RTX 2050 Laptop GPU** using `--method ipm --backend cuda` with cuBLAS, cuSPARSE, and cuSOLVER:

| Instance | Dimensions ($m \times n$) | Nonzeros | GPU Solver Time | IPM Iterations | Primal Residual $\|r_p\|$ | Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`agg.mps`** | $488 \times 163$ | 2,410 | **84.2 ms** | 30 | $1.91 \times 10^{-10}$ | **OPTIMAL (PASS)** |
| **`scagr7.mps`** | $129 \times 140$ | 420 | **42.6 ms** | 16 | $1.19 \times 10^{-11}$ | **OPTIMAL (PASS)** |
| **`scagr25.mps`** | $471 \times 500$ | 1,554 | **51.8 ms** | 15 | $3.62 \times 10^{-09}$ | **OPTIMAL (PASS)** |
| **`convex_qp.json`** | $0 \times 2$ | 2 | **28.4 ms** | 20 | $0.00$ | **OPTIMAL (PASS)** |

#### Performance Observations:
1. **Convergence**: CUDA IPM achieves excellent numerical stability, converging to residual tolerances below $10^{-10}$ on well-scaled constraint matrices.
2. **Transfer Overhead**: For small problems ($n < 500$), Host ◄► Device PCIe buffer copies dominate per-iteration cost. Transitioning to **persistent VRAM residency** will unlock full $5\times - 15\times$ speedups on large-scale instances.

---

### 5. Key Architecture Improvements Implemented

1. **Pseudo-Cost & Fractional Closeness Branching** (`cpp_solver/include/milp/BranchAndBound.hpp`):
   - Computes dynamic per-variable unit objective degradation estimates ($\mu_j^+, \mu_j^-$).
   - Combines with fractional closeness metric $f_j = |x_j - \lfloor x_j \rceil|$ for robust variable selection.
2. **Feasibility Pump Primal Heuristic**:
   - Integrates rounding and LP projection iterations at the root node to identify high-quality integer feasible incumbents before tree search begins.
3. **Native QPLIB Matrix Parser** (`sovereign_solver/qplib.py`):
   - Ingests standard `.qplib` formats, building Hessian $Q$, linear vector $c$, constraint matrix $A$, and variable bound structures.
4. **CUDA Build Automation** (`scripts/build_cuda.bat`):
   - Full MSVC 19.44 + NVCC 13.4 build pipeline linking cuBLAS, cuSPARSE, and cuSOLVER.

---

### 6. How to Reproduce All Benchmarks

```powershell
## 1. Build CPU Solver
cmake -S cpp_solver -B cpp_solver/build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build cpp_solver/build --config Release

## 2. Build GPU Solver (requires CUDA Toolkit and MSVC)
.\scripts\build_cuda.bat

## 3. Run all test suites and generate CSV/JSON results
py scripts/run_all_benchmarks.py
```

## Appendix 10: Improvements

Source document: [IMPROVEMENTS.md](IMPROVEMENTS.md)

## Sovereign Optimization Solver — Architectural Improvements & Technical Guide

**Optimization Engine Enhancements for SIH Benchmarking**\


---

### 1. Overview of Implemented Improvements

To significantly boost solver throughput, reduce branch-and-bound search tree size, and scale linear and quadratic programming capabilities, the following architectural upgrades have been designed and implemented in the **Sovereign Optimization Solver**:

```
                               ┌────────────────────────────────────────┐
                               │   SOVEREIGN ENGINE UPGRADES OVERVIEW   │
                               └────────────────────────────────────────┘
                                                   │
         ┌────────────────────────┬────────────────┼───────────────────────┬────────────────────────┐
         ▼                        ▼                ▼                       ▼                        ▼
┌──────────────────┐ ┌─────────────────────────┐ ┌──────────────────┐ ┌──────────────────┐ ┌────────────────────────┐
│ 1. MILP B&B      │ │ 2. CUDA GPU ACCEL.      │ │ 3. QPLIB PARSER  │ │ 4. NLA & PRESOLVE│ │ 5. DUAL SIMPLEX        │
│ Pseudo-Costs +   │ │ NVCC 13.4 + cuBLAS +    │ │ Native .qplib    │ │ Two-pass bound   │ │ Basis restoration &    │
│ Primal Heuristics│ │ cuSPARSE + cuSOLVER     │ │ ingestion format │ │ presolve check   │ │ fast warm-start path   │
└──────────────────┘ └─────────────────────────┘ └──────────────────┘ └──────────────────┘ └────────────────────────┘
```

---

### 2. Detailed Technical Breakdown

#### 2.1 Advanced Mixed-Integer Programming (`cpp_solver/include/milp/BranchAndBound.hpp`)

##### A. Pseudo-Cost Variable Scoring
- **Previous Bottleneck**: The solver used naive *first-fractional* variable branching, picking the first fractional index in variable order. This caused combinatorial explosion on MIPLIB models (e.g. 14,800+ nodes).
- **Implemented Upgrade**: Implemented **Pseudo-Cost & Fractional Closeness Scoring**:
  $$\mu_j^- = \frac{\sum \Delta z_j^-}{\sum f_j^-}, \quad \mu_j^+ = \frac{\sum \Delta z_j^+}{\sum f_j^+}$$
  $$score(j) = \max(\mu_j^- \cdot f_j, 10^{-6}) \times \max(\mu_j^+ \cdot (1 - f_j), 10^{-6}) + 0.1 \cdot (0.5 - |f_j - 0.5|)$$
  Variables that produce the steepest expected dual bound increase on both up and down branches are selected first.

##### B. Root Primal Heuristics
- At node 0, the solver automatically executes a bounded **Feasibility Pump** heuristic ($50$ projections/perturbations).
- If an integer-feasible solution is found, `incumbent` is initialized immediately before the first branch, enabling **aggressive branch pruning ($bound \ge incumbent$) across the entire tree**.

---

#### 2.2 CUDA GPU Acceleration (`cpp_solver/build-cuda/sovereign_presolve_cli.exe`)

The solver now compiles with **NVIDIA CUDA Toolkit 13.4** and runs directly on NVIDIA GPUs (e.g. **GeForce RTX 2050**, Compute Capability 8.6).

```
                      GPU ACCELERATED NUMERICAL BACKEND
  ┌───────────────────────────────────────────────────────────────────────┐
  │                         NVIDIA GeForce GPU                            │
  │                                                                       │
  │   ┌───────────────┐      ┌────────────────┐      ┌────────────────┐   │
  │   │    cuBLAS     │      │    cuSPARSE    │      │    cuSOLVER    │   │
  │   │  Dense GEMV,  │      │   SpMV (Ax),   │      │ Dense LU / KKT │   │
  │   │  AXPY, Dots   │      │   SpMV (Aᵀx)   │      │ Factorizations │   │
  │   └───────────────┘      └────────────────┘      └────────────────┘   │
  └───────────────────────────────────────────────────────────────────────┘
```

- **Dense Matrix/Vector Operations**: Handled via `cublasDgemv` and `cublasDdot`.
- **Sparse Constraint Operations**: Handled via `cusparseSpMV` in CSR format.
- **KKT Matrix Factorization**: Dense LU system solves executed via `cusolverDnDgetrf` and `cusolverDnDgetrs`.

---

#### 2.3 Native QPLIB Parser (`sovereign_solver/qplib.py`)

- Added dedicated native parser for `.qplib` text and AMPL format files from `qplib.zib.de`.
- Automatically extracts problem dimensions, variable types, linear objective coefficients, sparse quadratic terms $Q_{ij}$, and constraint bounds.
- Seamlessly integrates with the Newton-Barrier QP Interior Point solver.

---

### 3. How to Build & Run Updated Solvers

#### 3.1 Build CPU Solver (GCC + Ninja)
```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","User") + ";" + [System.Environment]::GetEnvironmentVariable("Path","Machine")
cmake -S cpp_solver -B cpp_solver/build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build cpp_solver/build --config Release
```

#### 3.2 Build GPU / CUDA Solver (NVCC 13.4 + MSVC)
```powershell
cmd /c scripts\build_cuda.bat
```

#### 3.3 Verify GPU Device Detection
```powershell
.\cpp_solver\build-cuda\sovereign_presolve_cli.exe --device-info
```

---

### 4. Benchmark Execution Commands

```powershell
## Netlib LP Benchmark (CPU Revised Simplex vs HiGHS)
py -m sovereign_solver.benchmark --dataset netlib --input benchmarks/netlib/small --solver cpp_solver/build/sovereign_presolve_cli.exe --tier quick --compare-highs --compare-presolve --output-dir results/netlib

## Netlib LP Benchmark (GPU CUDA IPM on RTX 2050)
py -m sovereign_solver.benchmark --dataset netlib --input benchmarks/netlib/small --solver cpp_solver/build-cuda/sovereign_presolve_cli.exe --method ipm --backend cuda --compare-highs --output-dir results/netlib_cuda

## MIPLIB 2017 Benchmark (Enhanced Pseudo-Cost B&B vs HiGHS)
py -m sovereign_solver.benchmark --dataset miplib --input benchmarks/miplib/small --solver cpp_solver/build/sovereign_presolve_cli.exe --limit 10 --method milp --compare-highs --output-dir results/miplib

## QPLIB Benchmark (Newton Barrier Convex QP)
py -m sovereign_solver.benchmark --dataset qplib --input benchmarks/qplib --solver cpp_solver/build-cuda/sovereign_presolve_cli.exe --method qp --output-dir results/qplib
```
