# Dataset-by-dataset benchmark report

This report inventories every model currently checked into `datasets/` and ties it to the saved benchmark evidence. It records only measured outcomes. **Not benchmarked** means no completed solve measurement was found in the checked-in reports; it does not imply that the model solves or fails. Timeout and unsupported outcomes are retained as results.

The detailed records are dated 2026-10-03 and were produced on Windows with an MSVC Release CPU build. The summary table in the final benchmark report distinguishes solver-only time from fresh CLI process wall time. Historical diagnostics may use different builds, limits, or timing scopes; those are labeled below and must not be compared as a controlled speed ranking.

## Summary

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

## LP: AFIRO

- **Input:** [`datasets/lp/small/afiro.mps`](../datasets/lp/small/afiro.mps), standard MPS; 3,327 bytes.
- **Model:** 32 continuous variables, 27 rows, 83 constraint nonzeros; minimize.
- **Result:** OPTIMAL in 16 revised-simplex iterations. Objective `-464.75314285714285`; original-model feasibility verification PASS.
- **Timing:** median internal solver time **0.13135 ms** and median fresh CLI wall time **16.6138 ms** over 10 runs. The difference includes process startup and output capture; use the solver time for optimizer timing.
- **Presolve:** 27×32 / 83 nonzeros → 25×32 / 81 nonzeros; two singleton reductions and two bound tightenings.
- **Reference context:** HiGHS 1.15.1 and Gurobi 13.0.3 returned the same objective on this model. Their timings have different API/process scopes and are not a controlled speed ranking.
- **Source note:** the timed final suite uses `benchmarks/netlib/small/afiro.mps` (3,244 bytes). Its parsed optimization model matches the `datasets/` AFIRO copy; the report’s SHA-256 is for the benchmark-copy file.
- **Full evidence:** [AFIRO benchmark report](LP_SMALL_FULL_BENCHMARK_20261003.md), [final benchmark report](FINAL_SOLVER_BENCHMARK_REPORT.md), [raw AFIRO run record](results/lp_small_full_benchmark.json).

## LP: 25FV47

- **Input:** [`datasets/lp/medium/25fv47.mps`](../datasets/lp/medium/25fv47.mps), 70,477 bytes.
- **Result:** The current repository parser identifies this file as Netlib EMPS-compressed MPS and rejects it because coefficient fidelity is not verified. No optimization solve, status, objective, or timing is claimed.
- **Next step:** provide a trusted standard-MPS copy, verify its parsed coefficients against the source, then add a bounded benchmark record. Do not treat a decode attempt as a solver result.

## LP: 80BAU3B

- **Input:** [`datasets/lp/large/80bau3b.mps`](../datasets/lp/large/80bau3b.mps), 298,952 bytes.
- **Result:** The current repository parser identifies this file as Netlib EMPS-compressed MPS and rejects it because coefficient fidelity is not verified. No optimization solve, status, objective, or timing is claimed.
- **Next step:** provide a trusted standard-MPS copy and validate the conversion before benchmarking it. The `large` directory label alone is not evidence of a completed large-LP solve.

## MILP: FLUGPL

- **Input:** [`datasets/milp/small/flugpl.mps`](../datasets/milp/small/flugpl.mps), 4,161 bytes; also present as the same-hash benchmark fixture under `benchmarks/miplib/small/`.
- **Model:** 18 variables (11 integer), 18 rows, 46 constraint nonzeros.
- **Result:** OPTIMAL in the final 5/5 runs; objective, incumbent, and best bound `1,201,500`; absolute and relative gap 0; original-model feasibility/integrality verification PASS.
- **Search:** median 5,367 processed nodes and 105,334 LP iterations in the final benchmark report. The later presolve-integrated optimization report records 96,253 LP iterations; these are separate report runs/configurations and must not be merged into one timing row.
- **Timing:** final benchmark report median solver **269.2061 ms**, fresh CLI wall **286.485 ms** (five runs).
- **Reference context:** the final report records objective agreement with HiGHS and Gurobi. HiGHS used 89 nodes; Gurobi presolved this case to an empty model. This demonstrates a large search-efficiency gap, not a claim that Sovereign is faster.
- **Full evidence:** [final benchmark report](FINAL_SOLVER_BENCHMARK_REPORT.md), [MILP optimization report](LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [historical MILP baseline](milp_baseline_20261003.md), [machine-readable result](results/FINAL_SOLVER_BENCHMARK_20261003.json).

## MILP: TIMTAB1

- **Input:** [`datasets/milp/small/timtab1.mps`](../datasets/milp/small/timtab1.mps), 53,477 bytes.
- **Model:** 397 variables, 171 rows, 829 constraint nonzeros; the parser identifies 171 integer variables.
- **Result:** a 30 s diagnostic is recorded as `TIME_LIMIT_NO_FINAL_RESULT`; an earlier 15 s external process cap also expired. No incumbent or final status was recorded in those runs. Therefore no optimality, infeasibility, or performance claim is made.
- **Historical diagnostics:** the baseline report contains an earlier 15 s externally terminated run. These are diagnostic attempts under different runs/limits, not one combined measurement.
- **Evidence:** [MILP baseline](milp_baseline_20261003.md), [MILP optimization report](LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [diagnostic JSON](results/LP_MILP_FINAL_OPTIMIZATION_20261003.json).

## MILP: traininstance2

- **Input:** [`datasets/milp/medium/traininstance2.mps`](../datasets/milp/medium/traininstance2.mps), 2,707,382 bytes.
- **Model:** 12,890 variables (7,880 integer), 15,603 rows, 41,531 constraint nonzeros.
- **Result:** `TIME_LIMIT_NO_INCUMBENT` at the recorded **30,000 ms** CPU run limit, while solving the root LP relaxation. One LP solve was entered; zero branch-and-bound nodes were processed; no incumbent, root bound, gap, or optimal solution is available.
- **Historical context:** the baseline also records a separately timed root-LP-only attempt and earlier runs with different time limits/build configurations. Those details are in the baseline report and should not be mixed with the 30 s final diagnostic.
- **Evidence:** [MILP baseline](milp_baseline_20261003.md), [MILP optimization report](LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [diagnostic JSON](results/LP_MILP_FINAL_OPTIMIZATION_20261003.json).

## MILP: supportcase6

- **Input:** [`datasets/milp/large/supportcase6.mps`](../datasets/milp/large/supportcase6.mps), 25,426,441 bytes.
- **Model:** 130,052 variables, 771 rows, 584,976 constraint nonzeros. The repository parser marks all variables integer.
- **Result:** `TIME_LIMIT_NO_INCUMBENT` at the recorded **60,000 ms** CPU run limit, during the root LP relaxation. One LP solve was entered; zero branch-and-bound nodes were processed; no incumbent, root bound, gap, or optimal solution is available.
- **Historical context:** the baseline’s older “parse/analyze only” result is not a solve benchmark. The final 60 s result is the run to use for the present solve-status summary.
- **Evidence:** [MILP baseline](milp_baseline_20261003.md), [MILP optimization report](LP_MILP_FINAL_OPTIMIZATION_REPORT.md), [diagnostic JSON](results/LP_MILP_FINAL_OPTIMIZATION_20261003.json).

## QP: QPLIB_9002

- **Input:** [`datasets/qp/small/QPLIB_9002.qplib`](../datasets/qp/small/QPLIB_9002.qplib), 203,064 bytes; QPLIB DCL.
- **Model:** convex minimization; 2,890 variables, 1,649 equality rows, 9,690 linear nonzeros; diagonal Hessian with 2,890 terms.
- **Result:** OPTIMAL in 43 Newton iterations and 43 linear-system solves, with 7,059 accumulated PCG iterations. Objective `5,698,097,498.146622`; original-model KKT verification PASS.
- **Timing:** final report median solver **377.4874 ms**, fresh CLI wall **455.0647 ms** over five runs. A separate QP optimization report uses a different run set and timing median; see that report rather than combining the values.
- **Backend:** the documented final solve ran on CPU. No QP reference solve completed in the report environment; no QP solver comparison is claimed.
- **Full evidence:** [QP baseline and fix report](QP_SMALL_BASELINE_AND_FIX_20261003.md), [final benchmark report](FINAL_SOLVER_BENCHMARK_REPORT.md), [machine-readable result](results/FINAL_SOLVER_BENCHMARK_20261003.json).

## QP: QPLIB_8906

- **Input:** [`datasets/qp/medium/QPLIB_8906.qplib`](../datasets/qp/medium/QPLIB_8906.qplib), 1,017,151 bytes.
- **Structure:** 5,223 variables, 838 rows, 15,558 linear terms, 56,975 Hessian terms (1,941 diagonal and 55,034 off-diagonal).
- **Result:** explicitly unsupported by the current QP input/solve path because general off-diagonal Hessian terms are not implemented. It was structurally inspected, not submitted as a solver run. No solver timing or objective is available.
- **Evidence:** [final benchmark report, medium-QP limitation](FINAL_SOLVER_BENCHMARK_REPORT.md#medium-qp-limitation), [machine-readable report](results/FINAL_SOLVER_BENCHMARK_20261003.json).

## QP: QPLIB_10038

- **Input:** [`datasets/qp/large/QPLIB_10038.qplib`](../datasets/qp/large/QPLIB_10038.qplib), 22,029,643 bytes.
- **Structure:** the repository’s Python QPLIB parser reads 160,800 variables, 160,400 rows, 959,203 linear terms, and 801 diagonal quadratic objective terms.
- **Result:** no solver run is recorded. These parser-level dimensions do not establish that the native QP algorithm can solve this large model. No status, objective, verification, or timing is claimed.

## Reproduction and timing rules

The final measured instances can be rerun with the commands in the [final benchmark report](FINAL_SOLVER_BENCHMARK_REPORT.md) and the [LP/MILP optimization report](LP_MILP_FINAL_OPTIMIZATION_REPORT.md). Use a Release build, preserve the input files, record the selected method/backend and run limits, and verify returned solutions against the original model.

- **Solver time** is the native solver's internal measurement.
- **CLI wall time** includes launching the process and capturing its output.
- Web backend/request time also includes adapter preparation and orchestration; it is a separate measurement scope.
- Timeouts are not successful solves. A timeout without incumbent has no objective or optimality certificate.
- `UNSUPPORTED`, `TIME_LIMIT`, and **not benchmarked** are distinct states.

## Related benchmark material

- [Final verified LP/MILP/QP report](FINAL_SOLVER_BENCHMARK_REPORT.md)
- [LP/MILP final optimization and timeout report](LP_MILP_FINAL_OPTIMIZATION_REPORT.md)
- [LP audit report and historical algorithm experiments](LP_AUDIT.md)
- [MIPLIB/FLUGPL historical baseline](milp_baseline_20261003.md)
- [AFIRO detailed repeated-run report](LP_SMALL_FULL_BENCHMARK_20261003.md)
- [QPLIB_9002 detailed QP report](QP_SMALL_BASELINE_AND_FIX_20261003.md)
- [Machine-readable benchmark results](results/)
