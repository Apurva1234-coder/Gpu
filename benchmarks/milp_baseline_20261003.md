# MILP Baseline — 2026-10-03

Baseline captured from the checked-in solver before MILP source changes. The original baseline was built with the Visual Studio 2019 toolchain in CMake `Debug` mode (the Ninja `--config Release` flag does not switch configurations). That fact was discovered after the first after-change runs; the optimized `Release` comparison is therefore reported separately below.

## Build and tests

- `sovereign_presolve_cli` and `sovereign_milp_branch_and_bound_tests`: baseline build succeeded.
- `ctest --test-dir cpp_solver/build -R milp_branch_and_bound_tests --output-on-failure`: passed, 13.67 s.

## Measured baseline

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

## Bottleneck evidence

- The two-variable smoke MILP solves correctly in milliseconds, so the basic B&B path can terminate on a trivial instance.
- FLUGPL's root LP is modest (16 iterations), but the default run is dominated by search: it takes 53.55 s to process 10,000 nodes and 186,317 LP iterations. A verified incumbent is eventually found, matching HiGHS' objective, but the solver stops at NODE_LIMIT with a 0.1813% gap instead of proving optimality.
- The FLUGPL run with a 100-node cap takes 640 ms total solver time, including root work and Feasibility Pump; the 1,000- and 3,000-node runs take 3.37 s and 11.84 s. Search cost grows with the number of node LP solves. At the 3,000-node point no incumbent has been found yet.
- Medium `traininstance2` has a separate severe LP-relaxation bottleneck: root relaxation alone exceeded 20 s after parse and presolve. More B&B changes alone cannot address this.
- Large `supportcase6` spends about 19 s just parsing before any solver-stage output. This baseline does not establish its solve performance.
- Code inspection corroborates the measurements: each branch copies a complete `Model` twice; every child LP rebuilds a relaxed `Model`; open nodes use repeated vector scans/erasures; Feasibility Pump solves a second root LP and may run 50 projection LPs; pseudocost histories are scored but never updated; and the native B&B has no wall-clock deadline.

## After first MILP changes

| Model | Result | Time and work |
|---|---|---|
| FLUGPL | OPTIMAL, objective 1,201,500, Verification PASS | 16,591.78 ms solver; 9,819 nodes; 9,819 LP solves; 190,579 LP iterations; zero gap |
| `flugpl.mps`, 3,000-node diagnostic | NODE_LIMIT, no incumbent yet | 5,772.41 ms solver vs 11,843.72 ms baseline at the same node cap |
| `traininstance2.mps`, 5,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 2,110 ms solver time; 1,946 ms root LP, 8 LP iterations; 3,163 ms parse+presolve |
| `traininstance2.mps`, 20,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 17,044.68 ms solver time; 16,874.70 ms root LP, 97 iterations; 3,163 ms parse+presolve; no B&B nodes |

The first after-change FLUGPL Debug solve improved from 53,545.59 ms / NODE_LIMIT / 0.1813% gap to 16,591.78 ms / OPTIMAL / zero gap, a 3.23x measured speedup within that initial measurement. It matches HiGHS' objective and passes original-model verification. This improvement comes from eliminating the duplicate root relaxation, capping the repeated Feasibility Pump projections, tracking observed pseudocosts, using a priority queue, and replacing per-open-node full models with shared branch-bound deltas and a single reusable relaxed base model.

Stage telemetry shows FLUGPL remains LP-bound: after the changes, node LPs consume about 14.3 seconds of the 16.5-second B&B stage. Warm-start attempts remain zero because the revised-simplex path rebuilds its basis and current branch-bound changes can alter standardized row structure. The implementation does not claim warm starts.

The medium `traininstance2` instance remains difficult. Its root LP alone consumes 16.87 seconds at the 20-second full-run limit in Debug mode. HiGHS 1.15.1 also reaches its 20-second limit on this model, returning an incumbent objective of 79,720 with about 99.96% relative gap; Sovereign returns no incumbent within that same total budget. This is an identified remaining gap, not a claimed fix.

## Optimized Release validation

After correcting the build configuration with `-DCMAKE_BUILD_TYPE=Release`, the same code and model were remeasured. These are not directly comparable to the original Debug baseline as a solver-code-only speedup, but they represent the runtime when built with compiler optimizations enabled.

| Model | Result | Optimized Release measurement |
|---|---|---|
| FLUGPL | OPTIMAL, objective 1,201,500, Verification PASS | 1,507.18 ms solver; 9,819 nodes / LPs; 190,579 LP iterations; zero gap. Earlier Debug run of the same changed code took 16,549.99 ms (about 11.0x slower). |
| `traininstance2.mps`, 5,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 5,107.98 ms process; 4,652.50 ms solver after 384.86 ms preparation; root LP 4,627.78 ms / 317 iterations; no B&B nodes. |
| `timtab1.mps`, 2,000 ms total budget, before heuristic guard | TIME_LIMIT_NO_INCUMBENT | 4,238.14 ms process; Feasibility Pump took 2,907.40 ms for one projection LP after the root LP. |
| `timtab1.mps`, 2,000 ms total budget, with heuristic guard | TIME_LIMIT_NO_INCUMBENT | 2,621 ms process; 2,514 ms solver; root LP 1,108 ms; two B&B nodes / LPs, 400 LP iterations; no incumbent. The optional pump was skipped. One child LP exceeded the remaining budget. |
| `supportcase6.mps`, 5,000 ms total budget | TIME_LIMIT_NO_INCUMBENT | 5,387 ms process; MPS parse 2,471 ms, presolve 453 ms, MILP preparation 2,924 ms, root LP 1,937 ms / 15 iterations. The run returned structured timeout status rather than spending the full ~19 seconds parsing before any result. |

Release resolves the severe Debug-build distortion on the small reference MIP. The medium MIP is now limited mainly by its root relaxation, and no feasible integer incumbent is found in five seconds. A short-budget guard avoids starting the optional Feasibility Pump when less than one second remains. The overall time limit is still cooperative: the `timtab1` run exceeded its two-second request by about 0.6 seconds while finishing a child LP, so it is not a strict wall-clock cap.

## Limitations of this baseline

The original Debug baseline does not provide stage-level B&B timings. Cooperative solver deadlines can be exceeded by an in-progress LP operation (shown by `timtab1` in the Release validation). The FLUGPL competitor comparison uses an external solver and is informational; no equivalent competitor run is recorded for the medium or large model in this report.

## Small-MILP optimization and ablation — 2026-10-03

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

## Small-MILP follow-up: profiling and parent dictionary reuse

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

### Pseudocost reliability ablation

The previous default waited for two observed branch outcomes in each direction before using a variable's pseudocost estimate. Changing that reliability threshold to one observation per direction (no probe LPs or strong branching) consistently changed the FLUGPL search tree and improved the cold path:

| Configuration | Solver ms (five runs; mean) | Node LP ms (mean) | Nodes / LP solves | LP iterations | First incumbent node | Result |
|---|---:|---:|---:|---:|---:|---|
| Cold + inherited-bound pruning + pseudocost threshold 1 | 261.45, 319.45, 287.11, 283.90, 299.11 (290.20) | 210.28 | 5,367 / 5,367 | 105,334 | 3,504 | OPTIMAL / 1,201,500 / PASS |
| Same, with parent dictionary warm starts enabled | 320.85, 344.13, 331.00, 339.01, 331.58 (333.31) | 249.04 | 5,367 / 5,367 | 20,185 | 3,504 | OPTIMAL / 1,201,500 / PASS |

Using one observation reduced the cold configuration's solver mean by 26.3%, nodes/LP solves by 23.4%, and LP iterations by 22.9% relative to the threshold-2 cold run above. Under the improved branching policy, parent dictionary reuse reduced iterations by 80.8% but was still 14.9% slower; it remains opt-in. The website default is cold.

After this policy change, the live website/API was run again against the rebuilt Release executable: OPTIMAL, objective 1,201,500, Verification PASS, CPU backend, 283.29 ms solver time and 319.30 ms backend request time, with 5,367 processed nodes/LP solves and 105,334 LP iterations.
