# Sovereign Optimization Solver — Benchmark Report

**Date:** 2026-10-03 · **Git HEAD:** `365209b88701d889d74e7e44a5ec20524782dbcf` · **Branch:** `solver-performance-telemetry`

## Executive Summary

The current Release CLI solved and verified three representative instances: Netlib AFIRO (LP), MIPLIB FLUGPL (MILP), and QPLIB_9002 (convex diagonal-Q QP). LP and MILP objectives matched installed HiGHS and Gurobi references. No completed reference QP result was available: Gurobi’s restricted license rejected the 2,890-variable model, and a HiGHS QP API trial returned no solution during the trial window.

The FLUGPL result is correct, but it required 5,367 processed nodes; HiGHS used 89, while Gurobi presolved the model to empty. Sovereign’s MILP presolve telemetry is currently not applied to the branch-and-bound input. QPLIB_9002 passed original-model KKT verification in 43 Newton iterations on CPU. QPLIB_8906 remains unsupported because it contains off-diagonal Hessian entries. These are prototype measurements, not broad production-performance claims.

## Environment and Method

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

## LP — Netlib AFIRO

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

## MILP — MIPLIB FLUGPL

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

## QP — QPLIB_9002

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

## Medium QP Limitation

QPLIB_8906 was structurally inspected only, not submitted to the solver. File: `datasets/qp/medium/QPLIB_8906.qplib`, 1,017,151 bytes, SHA-256 `29dd7c0295e04610d14b1c1abc6a0c8184400d2cbfc3b584695557cdc768976c`. It has 5,223 variables, 838 constraints, 15,558 linear NNZ, and 56,975 Hessian terms (1,941 diagonal, 55,034 off-diagonal). Current QPLIB parsing explicitly rejects off-diagonal Hessian terms. General sparse Hessian support and medium/large QP validation are future work.

## Combined Results

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

## Current Limitations

- The verified QP path is CPU-only here and accepts diagonal Hessians; off-diagonal sparse Hessians are rejected. QPLIB_8906 and medium/large QP are not validated.
- Only one MILP instance is measured; its tree is much larger than HiGHS/Gurobi’s. Current CLI presolve reductions are not passed to branch-and-bound.
- The benchmark covers one representative model per family and is not production-wide performance evidence.
- Unavailable telemetry: exact HiGHS presolved dimensions, LP dual/KKT metrics, simplex phase split and dense pivots/refactorizations, scale-factor ranges, presolve attempted counts/history sizes, and peak RSS.
- No successful QP reference result. Gurobi is license-limited; the HiGHS QP trial returned no solution.

## Demonstrated Capabilities

Native C++17 CLI parsing and CPU execution for these MPS LP/MILP and diagonal-Q QPLIB cases; revised-simplex LP; branch-and-bound with incumbent/bound/gap/node/LP telemetry; diagonal-Q Newton-barrier QP; presolve and scaling telemetry; postsolve; original-model LP/MILP verification; original-model QP KKT verification. The machine has an NVIDIA GPU and optional CUDA infrastructure exists in the repository, but these runs do not demonstrate GPU acceleration.

## Reproduction and Tests

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

## Conclusion

The measured instances support correctness for AFIRO, FLUGPL, and QPLIB_9002. The strongest next investigations are MILP tree size and connecting reported presolve reductions to the B&B model; for QPLIB_9002, linear-system work dominates. No optimization was performed in this reporting task.

## Demo Summary

- AFIRO: 32 variables; objective -464.75314285714285; PASS; 0.1314 ms median solver time.
- FLUGPL: 18 variables (11 integer); objective 1,201,500; PASS; 5,367 processed nodes; 269.2061 ms median.
- QPLIB_9002: 2,890 variables; objective 5,698,097,498.1466217; KKT PASS; 43 iterations; 377.4874 ms median.
- LP/MILP objectives agree with tested HiGHS and Gurobi results. No reference QP objective was obtained.
- All Sovereign runs used CPU; no GPU acceleration is claimed.
