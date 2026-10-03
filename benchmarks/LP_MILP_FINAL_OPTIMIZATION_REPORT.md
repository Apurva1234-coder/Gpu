# Sovereign LP / MILP Final Optimization Pass

**Measurement date:** 2026-10-03  
**Source revision tested:** `80e9ca80b1827f4dc90a20557ba67cccad3b0e4e`  
**Working tree:** solver and test edits listed below are uncommitted; existing server log files were already untracked. No commit or push was made.

## Executive summary

The code confirmed that the CLI calculated MILP presolve reductions but passed the original model to branch-and-bound. The pass now sends the integer-safe presolved model into B&B, restores the incumbent to original variable coordinates, and verifies it against the original model before reporting an optimal result. FLUGPL remains optimal and verified.

On ten interleaved FLUGPL runs per configuration, the integrated presolve reduced LP iterations from **105,334 to 96,253 (-8.6%)** and root LP iterations from **16 to 12**. It reduced the model from 18 variables / 18 rows / 46 nonzeros to 17 / 16 / 41. The B&B tree did **not** shrink: both configurations processed 5,367 nodes and solved 5,367 LP relaxations. Median solver time changed from 266.48 ms to 264.76 ms and median CLI wall time from 284.20 ms to 283.81 ms. Those time differences are within measurement variation; this pass does not establish a meaningful FLUGPL speedup.

A separate medium/large MILP failure was also fixed: B&B eagerly allocated a dense root LP before the LP solver could choose its sparse route. After removing that eager dense allocation for models over the existing memory budget, `traininstance2` and `supportcase6` reached sparse revised simplex. They still hit their 30-second and 60-second limits in the root LP without finding an incumbent. This changes an immediate dense-workspace failure into bounded solver progress, not into a completed solve.

Warm basis reuse cut FLUGPL LP iterations by about 79%, but was 15–18% slower in elapsed time on the measured runs. It remains opt-in and is not the default. A root rounding-and-repair experiment found no incumbent on FLUGPL and added an LP solve, so it was removed.

## Baseline and environment

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

## Diagnosis and implementation

### 1. MILP presolve now feeds branch-and-bound

Before the change, `main.cpp` ran `Presolver` and displayed its reductions, then called `BranchAndBound.solve(model, ...)` with the original model. The B&B root relaxation, every child relaxation, and the search tree therefore ignored those reductions.

The CLI now calls the presolve-aware overload. B&B solves the reduced model. Once it has an incumbent, the overload uses the existing fixed-variable and substitution history to reconstruct original coordinates, recomputes the original objective, and checks original bounds, integrality, and constraints. An optimal status is downgraded to numerical failure if that original-model check does not pass. The CLI performs its independent original-model check as well.

The MILP presolve mode protects discrete semantics:

- Singleton bounds on integer variables are rounded inward to integer bounds.
- Empty integer domains, including a nonintegral singleton equality, are recognized as infeasible.
- Equality substitution does not eliminate an integer variable, because that can create an unrepresented congruence condition.
- Fixed variables and continuous-variable substitutions remain reversible through the existing postsolve history.
- Relaxation, feasibility-pump, and verification loops ignore columns/rows that presolve marked inactive.

### 2. Oversized MILP root relaxations use the existing sparse route

B&B previously called dense `standardize(rootLPModel)` unconditionally. That call could throw the dense workspace guard before `LPSolver` got a chance to dispatch to sparse revised simplex. B&B now prepares and reuses a dense root structure only when the existing memory estimator says it fits the 256 MiB budget. Larger roots pass no dense prepared structure, allowing the existing `LPSolver` sparse route to run.

### 3. Root bound telemetry

The CLI now prints the root LP bound when the root relaxation is optimal. This makes root-bound and final-gap comparisons visible alongside the existing node, LP iteration, heuristic, cut, and timing metrics.

### 4. Deliberately not changed

No simplex, QP Newton, branching, pseudocost, cut-generation, or GPU algorithm was rewritten. No website changes were made. The LP code path for ordinary LPs is unchanged.

## FLUGPL controlled comparison

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

### Warm-start versus cold-start

Five-run FLUGPL samples with integrated presolve:

| Metric | Cold | Warm enabled |
|---|---:|---:|
| LP iterations | 96,253 | 19,876 (-79.4%) |
| Warm starts attempted / accepted / failed | 0 / 0 / 0 | 5,366 / 4,885 / 481 |
| Median solver time | 262.00 ms | 310.39 ms (+18.5%) |
| Median CLI wall | 283.57 ms | 327.40 ms (+15.4%) |
| Nodes processed | 5,367 | 5,367 |

The current warm state reuses a parent simplex tableau/basis and attempts dual reoptimization on child bounds. It materially reduces pivot iterations, but state reconstruction and reoptimization cost more wall time on FLUGPL. Warm starts stay opt-in. No sparse-LU, eta-update, or factorization architecture was added.

## LP and QP regression checks

| Dataset | Before | After | Result |
|---|---|---|---|
| Netlib AFIRO LP | 10 runs: median 0.1274 ms solver, 16.88 ms wall; 16 iterations | 10 runs: median 0.1251 ms solver, 16.13 ms wall; 16 iterations | OPTIMAL, PASS, objective -464.75314285714285; no LP algorithm changed |
| Netlib ADLITTLE LP | — | 140 iterations, 1.0302 ms solver, 23.98 ms wall | OPTIMAL, PASS, objective 225,494.96316237721 |
| Netlib AGG LP | — | 119 iterations, 15.2615 ms solver, 45.87 ms wall | OPTIMAL, PASS, objective -35,991,767.286576472 |
| QPLIB_9002 QP | — | 43 Newton iterations, 364.26 ms solver, 450.40 ms wall | OPTIMAL, KKT PASS; objective 5,698,097,498.1466217 |

AFIRO row/column/NNZ statistics were 27/32/83 before presolve and 25/32/81 after it. Its objective and iteration count did not change. The QP algorithm was not modified; its final primal residual was `7.59021848e-9`, dual residual `2.43429411e-16`, and complementarity `4.69230348e-16`.

## Additional MILP diagnostics

The sparse-root fix was checked against the medium and large repository MILPs with the requested bounded budgets. These did not reach B&B because their root LPs did not finish in time.

| Dataset | Structure | Presolve / root LP | Bounded result |
|---|---|---|---|
| `traininstance2.mps` (medium) | 12,890 vars, 15,603 rows, 41,531 NNZ | 12 vars fixed; 12,878 active vars; root sparse revised simplex ran 2,370 iterations | `TIME_LIMIT_NO_INCUMBENT` at 30 s; no B&B nodes |
| `supportcase6.mps` (large) | 130,052 vars, 771 rows, 584,976 NNZ | no presolve reductions; root sparse revised simplex ran 865 iterations | `TIME_LIMIT_NO_INCUMBENT` at 60 s; no B&B nodes |
| `timtab1.mps` (small-tier MIPLIB file) | 397 vars, 171 rows, 829 NNZ | baseline parse/presolve completed; 13 variables were fixed | 30-second diagnostic expired without a final solver result; not used in the performance comparison |

Before the sparse-root fix, `traininstance2` and `supportcase6` exited after parsing/presolve with a dense-workspace refusal. Afterward they return controlled time-limit statuses with LP iteration progress. They still need faster sparse root relaxations and, for larger instances, an incumbent heuristic that can find feasible integer points. A timeout is not an unsupported-model claim.

The repository contains no `p0033`, `bell3a`, `egout`, `enigma`, `mod008`, or `stein27` files, so none were added or downloaded.

## B&B strategy and what remains

- **Branching:** existing pseudocost score (`down estimate × up estimate`) with a fractional-violation tie-break; no branching change was made.
- **Node queue:** existing best-bound-first selection; no node-selection change was made.
- **Root bound/cuts:** FLUGPL root bound was 1,167,185.7255923203; the final objective/bound was 1,201,500. No cuts were generated, so root bound strength did not change.
- **Incumbent heuristics:** current root rounding and feasibility pump did not improve FLUGPL's first incumbent. A single root rounding/continuous reoptimization experiment also failed to find an incumbent on FLUGPL and added an LP solve, so it was removed.
- **Warm LP reoptimization:** iteration count improves sharply but wall time regresses on FLUGPL; needs lower per-start overhead or a selective policy before becoming default.
- **Remaining FLUGPL bottleneck:** 5,367-node search tree and 96,253 LP iterations remain; presolve did not reduce the tree.
- **Remaining medium/large bottleneck:** sparse root revised simplex did not finish within 30/60 seconds. These runs spent their budgets in the root LP and produced no incumbent or B&B progress.
- **LP numerical/performance work:** AFIRO, ADLITTLE, and AGG passed. This pass did not change LP pivoting or factorization; only MILP root dispatch now allows large relaxations to reach the existing sparse route.
- **Coverage limitation:** these are prototype results on a small set of checked-in instances, not a production solver certification.

### Reference solver data

HiGHS and Gurobi were not installed or discoverable in this runtime, so no new reference run was made. The repository’s earlier benchmark report records independent historical comparisons for AFIRO and FLUGPL (tested on an earlier revision): HiGHS 1.15.1 used 89 nodes on FLUGPL; Gurobi 13.0.3 presolved that instance to empty. Those are prior measurements, not results from this run. They are reported as context only; no solver ranking is claimed.

## Validation and reproduction

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

## Files changed

Solver/test changes are limited to:

- `cpp_solver/include/presolve/Presolver.hpp`
- `cpp_solver/include/milp/BranchAndBound.hpp`
- `cpp_solver/include/milp/LPRelaxation.hpp`
- `cpp_solver/include/milp/FeasibilityPump.hpp`
- `cpp_solver/src/main.cpp`
- `cpp_solver/tests/presolve_tests.cpp`
- `cpp_solver/tests/milp_branch_and_bound_tests.cpp`

The corresponding machine-readable run data is in [`LP_MILP_FINAL_OPTIMIZATION_20261003.json`](results/LP_MILP_FINAL_OPTIMIZATION_20261003.json). No solver changes were committed or pushed.
