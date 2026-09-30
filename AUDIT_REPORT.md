# Sovereign solver prototype audit

**Audit date:** 2026-09-30  
**Scope:** current working tree, the executable selected by the web API, checked-in benchmark records, unit tests, and direct local benchmark runs.  
**Rule followed:** no solver or UI implementation was changed during this audit.

## Executive result

Sovereign is a genuine prototype solver with working verified paths for small LP and diagonal convex QP problems. The CUDA executable is real and the QP path genuinely calls cuSOLVER. It is **not ready to claim broad large-scale LP/MILP capability**. The most urgent gaps are the non-configurable MILP 10,000-node cap, the large-MPS C++ parsing path, false completion stages in the UI, incomplete telemetry, and benchmark/documentation claims that disagree with recorded evidence.

### Runtime evidence collected in this audit

| Input / path | Result | Mathematical check |
|---|---|---|
| Netlib `afiro.mps`, Revised Simplex CPU | `OPTIMAL`, 18 iterations, objective `-464.75314285714285` | `Verification: PASS`, maximum feasibility error `1.42e-14` |
| Mittelmann `afiro.mps`, Revised Simplex CPU | `OPTIMAL`, 18 iterations, same objective | `Verification: PASS`, maximum feasibility error `1.42e-14` |
| Convex QP JSON, QP Newton/Barrier CUDA | `OPTIMAL`, 13 iterations, objective `2.6476763e-08` | `Verification: PASS`; primal and dual residuals `0`; complementarity `6.62e-09` |
| MIPLIB `flugpl.mps`, Branch-and-Bound CPU | `ITERATION_LIMIT`, 10,000 nodes processed, feasible incumbent `1201500` | `Verification: PASS`; remaining absolute gap `2225` |
| Uploaded `supportcase6.mps` (25.4 MB) | C++ process failed after preprocessing | Python analysis completed: 130,052 variables, 771 rows, 584,976 linear nonzeros; no candidate was returned |

The CUDA runtime reported one usable device: **NVIDIA GeForce RTX 3050 Laptop GPU**, compute capability 8.6, CUDA 13.1.

---

## A. FULLY WORKING

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

## B. PARTIALLY WORKING

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

## C. BROKEN / BUGS

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

## D. NOT IMPLEMENTED

| Req. | Affected files | Reason | Severity | Smallest safe fix |
|---|---|---|---|---|
| 20–21 | Dual Simplex/IPM and core linear algebra | No production sparse basis update/factorization path is used by Dual Simplex/IPM; no million-scale evidence exists. | HIGH | Add sparse factorization integration before making large-model claims. |
| 26 | CLI/API verification schema | Consistent bound/integrality/objective/KKT reporting across LP, QP and MILP is absent. | MEDIUM | Define shared verification result schema. |
| 29 | `solver_policy.py` | Approximate 60/120/300 automatic time budgets are not configured. | HIGH | Configure and test policy limits. |
| 41–42 | CUDA telemetry | Kernel time, transfer time, memory consumption and exact operation attribution are not emitted. | MEDIUM | Add CUDA events and structured output. |
| 73 | Benchmarks / result records | No audited real benchmark MILP was proven optimal; the requirement for one verified LP, MILP and QP demonstration is incomplete. | HIGH | Select a small MIPLIB instance that terminates or present a verified feasible incumbent clearly as non-optimal. |
| 76–77 | README/demo claims | No million-variable demonstration or defensible scalability evidence. | HIGH | State this as future work only. |

---

## E. IMPLEMENTED BUT NOT CONNECTED TO WEBSITE

| Req. | Affected files | Reason | Severity | Smallest safe fix |
|---|---|---|---|---|
| 7 | `CuttingPlane.hpp`, `main.cpp`, `solver_policy.py` | Manual expert method only; no automatic integration. | MEDIUM | Integrate with policy or hide behind explicit experimental label. |
| 8 | `FeasibilityPump.hpp` | Root heuristic is called internally but its iterations, projections and result are not surfaced. | MEDIUM | Include heuristic metrics in `MILPResult` and UI. |
| 22 | `Factorization.hpp` | Cholesky implementation exists but was not traced into an active production QP/LP route. | LOW | Remove unused claim or integrate and test it. |
| 41–42 | `CudaBackend.cu` | cuBLAS/cuSPARSE wrappers are present but no UI shows which operation actually used which library. | MEDIUM | Return operation-level CUDA telemetry. |
| 65–67 | `main.cpp`, `app.js` | Several produced fields are discarded by `parse_solver_output` or never rendered. | MEDIUM | Prefer a machine-readable solver result. |

---

## F. GPU/CUDA STATUS

**Real working status:** CUDA is available and the website resolves `backend=cuda` to `cpp_solver/build-cuda/sovereign_presolve_cli.exe`. Runtime `--device-info` confirmed the RTX 3050 Laptop GPU and CUDA 13.1.

**What really runs on the GPU:** the QP KKT dense linear solve calls `cuda::Context::solveDense`, which uses cuSOLVER. CUDA source also implements cuBLAS AXPY/dot/GEMV and cuSPARSE CSR SpMV, with host-device copies on every call. The audited QP CUDA run was verified, so this is genuine CUDA execution, though it is not fully resident on device and has no measured speedup evidence.

**What remains CPU:** Revised Simplex, Dual Simplex, branch-and-bound control, presolve, parser, postsolve, verification, and automatic MILP. The policy states this correctly. No GPU speedup should be claimed from the current `results/` records because every recorded CPU/GPU pair count is zero.

---

## G. LP STATUS

Small real LP solving is working and verified. AFIRO passes both Netlib and Mittelmann source locations. Revised Simplex measures all stages and applies original-model verification after postsolve.

LP remains unsuitable for broad performance claims: the recorded Netlib summary has 1 verified success from 10 runs, 1 timeout and 6 other failures. Large LP handling has a conditional sparse revised-simplex path, but other LP methods still rely on dense matrices. The CPU binary reports `Build type: configuration unspecified`, so release compilation should be standardized before measuring performance.

---

## H. MILP STATUS

MILP is a real LP-relaxation branch-and-bound prototype with best-bound selection, pseudocost scoring, root feasibility pump, pruning, gap fields, and original-model feasibility/integrality verification. The direct `flugpl.mps` run produced a verified incumbent.

It is currently capped at 10,000 B&B nodes independently of the UI or `--max-iterations`, so the visible configuration is misleading. The recorded MIPLIB report has no verified successful instance. Large MIPLIB parsing/solve failures have insufficient error detail. Treat MILP as an experimental small-instance capability until node limits, parsing, and structured status reporting are corrected.

---

## I. QP/QPLIB STATUS

QP classification checks convexity and the CLI uses a Newton/barrier KKT solve. The CUDA QP test verified a solution. The previous QPLIB-9002 run correctly classified QP and preserved 2,890 variables, 1,649 constraints, and 12,580 total nonzeros before reaching a verified optimum in 22 iterations.

The supported QP envelope is narrower than "standard QPLIB" in general: the canonical JSON/C++ input supports diagonal quadratic terms, and `GeneralQP` rejects variables without a finite bound and unsupported nonconvex structure. Raw QPLIB is parsed in Python and converted to canonical JSON for the C++ executable. This should be documented as a supported subset and protected with official raw QPLIB regression fixtures.

---

## J. NUMERICAL ROBUSTNESS STATUS

Implemented protections include tolerance centralization, pivot/singularity thresholds, finite checks, Harris-style ratio protection in simplex, residual checks, original-model LP verification, QP convexity checks, and variable scaling in the general QP transform. The QP output reports primal/dual/complementarity values.

Missing evidence is more important than missing classes: no comprehensive ill-conditioned/degenerate benchmark battery validates these protections, and statuses are not normalized across LP/QP/MILP. The highest risk is dense memory and repeated factorization in large methods, not a single tolerance constant.

---

## K. BENCHMARK STATUS

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

## L. UI/TELEMETRY ISSUES

1. The front-end makes every pipeline stage look complete after a result, even when solving failed or timed out. This violates truthful status reporting.
2. The live bar is indeterminate animation. It reports real elapsed wall time but no solver iterations or stage events.
3. QP and MILP omit several timing values because their CLI branches do not emit them.
4. Metric cards can turn unavailable values into `0`, which is indistinguishable from a measured zero.
5. The Variable Explorer is functionally correct but the analysis response sends every variable’s metadata. This is expensive for 130k variables and should be paginated server-side.
6. The heatmap is correct: it uses real matrix buckets, has an unavailable state, and is bounded at 50×50. Its API payload should drop legacy sampled points when the heatmap is the only consumer.

---

## M. TEST RESULTS

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

## N. CRITICAL FIXES BEFORE DEMO

1. **Fix B&B control and status reporting** — add a node-limit argument, wire it through policy/API/CLI, and make the UI display `ITERATION_LIMIT`/`NODE_LIMIT` accurately.
2. **Fix large MPS parsing and failure diagnostics** — eliminate rows×variables reconstruction, preserve sparse rows while parsing, and return an explicit parse/solver failure reason.
3. **Make pipeline stages truthful** — backend returns per-stage states; UI uses `completed`, `running`, `failed`, or `skipped`, never blanket `done`.
4. **Remove or regenerate unsupported benchmark claims** — replace `BENCHMARKS.md` speed/MIPLIB assertions with the actual records currently presented by the site.
5. **Finish telemetry** — emit timing and available solver metrics in every solve branch; use `N/A` for unavailable values.
6. **Reduce API payload size for large models** — heatmap summary by default, server-paginated metadata/solution rows on demand.

---

## O. POST-DEMO ROADMAP

1. Establish a release build through CMake/Ninja or a documented MSVC build, then capture compiler flags and dependency versions in every benchmark.
2. Define one structured result protocol from C++ to API rather than parsing text logs.
3. Complete sparse factorization/update integration for large LP, Dual Simplex and IPM; set memory thresholds from measured data.
4. Expand QP support beyond the verified diagonal/bounded subset, with official QPLIB regression corpus and convexity checks.
5. Improve MILP with configurable node/time/gap limits, safe cut integration, heuristic telemetry, and a curated verified MIPLIB suite.
6. Add CUDA event/memory instrumentation, persist data on device where algorithms benefit, and publish CPU/GPU measurements only from matched verified runs.
7. Add integration tests for raw QPLIB upload, 25–128 MB MPS upload, cancellation, time limit, node limit, and UI stage truthfulness.

## Requirement disposition index

**Implemented/verified:** 1–3, 6, 9–12, 16–17, 24–25, 28, 30–32 for LP/QP, 37–40, 43–47, 50–64, 78–79, 84–87, 88–90.  
**Partial or unverified:** 4–5, 7–8, 13–15, 18–23, 26–27, 29, 31 for MILP, 33–36, 41–42, 48–49, 65–75, 80–83, 91–92.  
**Broken/incorrect:** 15/20 for large MPS, 31 for MILP configuration, 35/65–67 metrics, 52 unavailable-to-zero rendering, 81–83 stage truthfulness, and 93–94 benchmark documentation claims.  
**Not demonstrated:** 73, 76–77 and all broad performance/scalability claims.
