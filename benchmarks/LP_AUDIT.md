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

## Phase 3 changes and measurement

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

## Phase 4 changes

- Added a conservative dense peak-workspace estimator for standardized form, tableau, standard matrix, basis, and vector/row storage. The shared limit is 256 MiB.
- Revised Simplex routes to its existing sparse implementation before dense standardization if the estimate exceeds the limit. Dual Simplex and Mehrotra IPM return `UNSUPPORTED` with the estimate and budget instead of allocating dense matrices. Direct dense `standardize()` also rejects over-budget requests.
- CLI, benchmark CSV/JSON, and Auto attempt telemetry now include the estimate, budget, and whether the guard triggered. The Auto attempt panel reports memory routing and the solver message.
- Added a 4,000-row × 4,000-column sparse regression fixture with 4,000 nonzeros. Its estimated dense peak is about 977.9 MiB; tests confirm Revised Simplex selects sparse, Dual Simplex/IPM refuse, and direct dense standardization throws before allocation.

The memory guard is about safety, not a performance result. The synthetic fixture only checks route/guard behavior and does not establish general 4,000 × 4,000 solve performance. CUDA free-memory admission checks and full large-model memory profiling remain open.

## Phase 5 algorithm-selection measurements and policy

An MSVC x64 Release executable was used for three runs each on AFIRO, Bandm, Scagr25, and Pilot, with presolve on, a 2,000-iteration cap, and an 8-second process timeout. Raw Revised Simplex and PDHG measurements are in `lp_audit_phase5_revised.json` and `lp_audit_phase5_pdhg.json`; Dual Simplex measurements are in `lp_audit_phase5_dual.json`.

| Model | Revised Simplex median solve ms / status | PDHG median solve ms / status | Dual Simplex median solve ms / status |
|---|---:|---:|---:|
| AFIRO | 0.226 / OPTIMAL, verification PASS | 1.828 / ITERATION_LIMIT | 27.853 / OPTIMAL |
| Bandm | 250.245 / NUMERICAL_FAILURE | 59.504 / ITERATION_LIMIT | 3,131.772 / NUMERICAL_FAILURE |
| Scagr25 | 707.882 / NUMERICAL_FAILURE | 51.724 / ITERATION_LIMIT | TIMEOUT at 8 s |
| Pilot | 507.420 / NUMERICAL_FAILURE | 688.654 / ITERATION_LIMIT | UNSUPPORTED by dense-memory guard |

PDHG was faster on Bandm and Scagr25 in this bounded diagnostic, but did not produce a verified solution on any of the four instances; it was slower on Pilot and AFIRO. Dual Simplex was substantially slower on AFIRO and Bandm, timed out on Scagr25, and was correctly refused for Pilot. These results do not support a broad PDHG or Dual Simplex default. The pilot Auto policy therefore retains Revised Simplex for ordinary small/medium structures and routes only very large sparse LPs (at least 100,000 columns, 10,000 rows, 1,000,000 entries, and 99.5% sparsity) to sparse PDHG as the scale-oriented algorithm. It disables a second full-solve fallback on that route. That threshold is a transparent routing policy, not an empirically established speed crossover; status and original-model verification remain authoritative. CPU is retained because no paired, verified CPU/CUDA crossover has been established.

No million-scale input is present in the checked-in benchmark corpus. Phase 5 validates the dispatch decision with synthetic analysis metadata only; it does not claim million-scale parse, memory, convergence, or solve performance.

## Phase 6 sparse Revised Simplex profile and experiment

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

## Phase 7 CPU PDHG measurements

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

## Phase 8 CUDA PDHG audit

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

## Phase 9 PDHG step-weight experiment

The native PDHG implementation uses a fixed relative primal/dual step weight of 1. The current benchmark residuals showed an imbalance between primal and dual progress, so a diagnostic tested a fixed initial weight of `||c||₂ / ||b||₂` (clamped to `[1e-4, 1e4]`) while keeping the product of the diagonal primal and dual step sizes unchanged. The C++ result and CLI now expose the chosen weight so future experiments are observable.

The norm-ratio weight was rejected as a default. On 20,000-iteration CPU runs, all four cases still hit the cap. Pilot solve time changed from 5,745 ms to 5,223 ms, but its dual residual worsened from 0.869 to 0.999 and complementarity from 0.759 to 0.997. Bandm's dual residual worsened from 0.477 to 0.642. Stocfor2's dual residual improved from 0.469 to 0.083, but complementarity worsened from 0.112 to 0.765. Agg remained effectively unchanged. At 2,000 iterations, AFIRO, Small, Pilot, and Stocfor2 also remained `ITERATION_LIMIT`. Because the change trades progress among KKT conditions and does not consistently produce a verified solution, the solver keeps weight 1. Raw data is in `lp_audit_phase9_weight_cpu_20k.json` and `lp_audit_phase9_weight_cpu_2k.json`.

This result is consistent with the limits of changing only a fixed weight: practical large-scale PDHG research combines adaptive step sizes, restart logic, diagonal preconditioning, and presolve; the reference method is described in the [PDLP paper](https://arxiv.org/abs/2106.04756). This repository does not use that external solver or its code. The native solver still lacks a tested adaptive step/restart scheme, and the current experiments do not justify a claim that its PDHG path solves medium or large LPs.

## Phase 9 broader native LP-method profile

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
