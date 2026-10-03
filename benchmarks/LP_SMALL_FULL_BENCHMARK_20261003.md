# Small LP End-to-End Baseline — 2026-10-03

## Scope and safeguards

This is a read-only solver baseline of the existing Release executable. The only new files are this report and its JSON companion. No solver, website, QP, MILP, tests, or benchmark inputs were changed; no build, commit, or push was performed. The worktree was already dirty at start (including existing QP/UI modifications); baseline Git HEAD was `365209b88701d889d74e7e44a5ec20524782dbcf`. Preserve those pre-existing changes.

## Dataset selection

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

## Original model, validation, and classification

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

## Build and solver configuration

- Executable: `cpp_solver/build-route-cpu/sovereign_presolve_cli.exe` (479,232 bytes; timestamp 2026-10-03 13:21 local)
- CMake cache: Release, Ninja generator, MSVC compiler path using toolset 14.29.30133; Release flags `/O2 /Ob2 /DNDEBUG`.
- No rebuild was performed, so the measured binary is the existing binary and this report does not claim a fresh reproducible build from current source.
- Reproduction build command (not run for this baseline): `cmake --build cpp_solver/build-route-cpu --target sovereign_presolve_cli`.
- Method: Revised Simplex; CPU; presolve on; iteration limit 10,000; no LP time limit; feasibility and optimality tolerances 1e-8.
- Threads, exact pivot/refactorization policy, and phase-specific pivot counts are not exposed by this CLI. It is a fresh process with no LP warm start.
- Standardization scaling is enabled: one row max-norm pass followed by one column max-norm pass. Scale-factor ranges and post-scaling coefficient ranges are not instrumented.
- Dense memory estimate: 81,400 bytes, under the 268,435,456-byte budget; memory guard not triggered. Peak resident process memory was not measured.

## Presolve and standardization

| Model measure | Original | Presolved | Change |
|---|---:|---:|---:|
| Rows | 27 | 25 | -2 (-7.407%) |
| Columns | 32 | 32 | 0 (0%) |
| Matrix nonzeros | 83 | 81 | -2 (-2.410%) |

Measured presolve operations: 2 singleton reductions, 2 bound tightenings, 0 fixed variables, 0 substitutions, 0 eliminated variables, and 0 redundant rows. Two passes ran. The CLI does not expose empty-row/column attempted counts, identities of rows removed by singleton reductions, equality/inequality totals after presolve, presolved-space objective separately, objective-offset history, or postsolve stack sizes. No variables were eliminated/fixed/substituted, so postsolve reconstructed the original 32-variable solution without variable-value reversals. The objective constant is zero, so no objective offset is apparent in the input.

Presolve reduced the model's row count, but the solver's standardized tableau still had 35 rows, 32 columns, and 117 nonzeros. The no-presolve run had the same standardized dimensions and nonzero count. This happens because standardization expands each equality into two inequalities and adds rows for finite upper bounds; presolve tightened two upper bounds while removing two singleton rows. Thus the presolve counts do not translate into a smaller standardized dense tableau on this instance.

## Ten-run native benchmark

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

## Original-space independent verification

For every native run, the 32 returned variable values were matched to the original MPS variable names. Independently recomputed `c^T x + constant` was `-464.75314285714285`, exactly matching the displayed objective to the printed precision. Maximum original-row violation was `5.684341886080802e-14`; maximum bound violation was 0. The CLI's own original-model feasibility verification was PASS. It does not expose LP dual feasibility, reduced-cost residual, or complementarity for this revised-simplex path; these are N/A, not zero.

## Reference solvers (same AFIRO MPS)

Installed in the existing Python environment: HiGHS 1.15.1 and Gurobi 13.0.3. CPLEX was not installed, GLPK and SCIP commands were not found. `clp` resolved to a PowerShell alias rather than an executable, so CLP was not benchmarked. No packages were installed. Both references used one thread and their default presolve/settings. Times below use 10 API calls; their optimize-call wall excludes process startup and Python interpreter startup, unlike the fresh native CLI wall time.

| Solver | Status | Objective | Difference from Sovereign | Presolved rows / cols / nnz | Method / iterations | Reported solve time | Median optimize-call wall | Median model-read wall | Fresh Python process wall |
|---|---|---:|---:|---|---|---:|---:|---:|---:|
| Sovereign | OPTIMAL | -464.75314285714285 | 0 | 25 / 32 / 81 | Revised simplex / 16 | median 0.1279 ms | N/A (native fresh process) | included in CLI wall | median 16.6513 ms fresh CLI |
| HiGHS 1.15.1 | kOptimal | -464.75314285714285 | 0 | N/A / N/A / N/A | automatic simplex / 6 | median 0.6264 ms | median 0.6449 ms | median 0.3587 ms | median 330.0122 ms fresh Python process |
| Gurobi 13.0.3 | OPTIMAL | -464.75314285714285 | 0 | 6 / 9 / 26 | automatic / 2 | 0.0000 ms (timer resolution) | median 0.3346 ms | median 0.4199 ms | median 184.5286 ms fresh Python process |

HiGHS reported presolve status `kReduced`, but its Python API returned an empty presolved LP after the solve, so no dimensions are claimed. Gurobi's `presolve()` returned 6 rows, 9 columns, 26 nonzeros. The presolvers demonstrably produce different reduced models; counts alone do not prove equivalence of their internal representations. The iteration counts are not directly comparable because the methods and presolve reductions differ. Gurobi's time rounded to zero at its reported timer resolution; use API wall for this tiny instance. Gurobi ran under its restricted/non-production license, which permits this small model.

These scopes are not a fair speed contest. Sovereign is a native executable; reference fresh-process measurements also start Python and import solver packages (median 330.0 ms HiGHS, 184.5 ms Gurobi), while in-process API timings exclude that overhead. Solver-reported values are each library's own clocks. The measured evidence only says all three gave the same optimum; it does not support a winner claim.

## Presolve-off ablation (one run)

| Setting | Standardized rows / cols / nnz | Iterations | Solver ms | Pipeline ms | Objective | Verification |
|---|---:|---:|---:|---:|---:|---|
| Presolve on (representative run) | 35 / 32 / 117 | 16 | 0.1264 | 0.1948 | -464.75314285714285 | PASS |
| Presolve off (single run) | 35 / 32 / 117 | 16 | 0.1336 | 0.2071 | -464.75314285714285 | PASS |

The single no-presolve run is too small/noisy to claim a timing benefit. It confirms the standardized dimensions and iteration count are unchanged. There is no safe CLI option to disable scaling, so scaling's effect cannot be isolated in this baseline.

## Machine

Windows NT 10.0.26200.0; AMD64 Family 25 Model 68 Stepping 1 (AuthenticAMD); 16 logical processors; 16,989,736,960 bytes installed RAM. GPU not used.

## Primary summary

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

## Findings and next bottleneck (no optimization performed)

1. Sovereign solves AFIRO correctly in these 10 runs; its original-model verification passes.
2. Presolve removes 2/27 rows and 2/83 matrix entries, but not columns. Standardized solver dimensions remain unchanged.
3. Among separately instrumented internal stages, parse takes around 0.7–1.5 ms in these samples; solver-only is about 0.12–0.18 ms. Fresh process wall time (median 16.65 ms) dominates tiny-model end-to-end time.
4. The one-run presolve-off ablation does not establish a speed benefit. It also has the same standardized dimensions and iterations.
5. Scaling is on, but its benefit cannot be measured without a supported off switch and separate controlled repetitions.
6. All three objective values match exactly to displayed precision. Gurobi's presolve reduces the model further by its reported dimensions than Sovereign's; HiGHS reported reductions but its exact dimensions were unavailable through the API used.
7. The next investigation for this tiny benchmark should be process/startup overhead and measurement methodology, followed by solver scaling/algorithm quality on substantially larger sparse models. This report does not start that work.

### Telemetry gaps

CLI output does not expose phase-I/phase-II splits, pivot counts for dense simplex, detailed scaling factors, presolve attempted counts/row identities/history sizes, remaining presolved row categories, exact presolved HiGHS dimensions, dual/KKT residuals for revised simplex, or peak RSS. These are marked unavailable in the companion JSON rather than fabricated.


