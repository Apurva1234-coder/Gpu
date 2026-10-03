# Small QP baseline and optimization

## Scope and target

This phase covers only the repository's smallest representative QP, `QPLIB_9002.qplib` (in `datasets/qp/small`). The instance has 2,890 variables, 1,649 equality constraints, 9,690 linear constraint nonzeros, and a diagonal Hessian with 2,890 nonzeros. Its objective is convex, minimization, and has no linear terms or objective offset. The UI labels this instance MEDIUM under its solver-size policy even though it is in the `small` QP dataset directory.

QPLIB defines the quadratic objective as `1/2 x^T Q x + b^T x + q`; the parser and solver now preserve its objective offset `q`. Sources: [QPLIB format documentation](https://qplib.zib.de/doc.html), [QPLIB_9002 instance](https://qplib.zib.de/QPLIB_9002.html).

## Release baseline

The solver and tests were built using the repository's MSVC Release build (`/O2 /Ob2 /DNDEBUG`). Before the algorithm change, five direct CLI runs limited to one Newton iteration gave a median solver time of **9,394.54 ms** (range **9,296.75–9,539.37 ms**). Profiling showed **9,265.66 ms** in the linear solve. The implementation formed and factored a dense Schur matrix for 1,649 equality rows. That cubic dense factorization, rather than parsing or postsolve, was the dominant cost.

## Change and comparable timing

The QP solver now builds Schur products from sparse row/column data and solves larger Schur systems with preconditioned conjugate gradients (PCG), avoiding the dense `p × p` factorization. Diagonal Hessian convexity is checked directly instead of allocating a dense `n × n` matrix. The dense path remains for small Schur systems. The QP Auto route uses CPU because this PCG path has no CUDA implementation and no measured GPU benefit for this instance.

For the same one-Newton-iteration CLI scope, five Release runs had a median of **6.14 ms** (range **5.68–8.53 ms**), about **1,530× faster** than the dense baseline for that like-for-like iteration. The full solve converged in **43 Newton iterations** (43 linear-system solves; 7,059 accumulated PCG iterations). Five full solves had a median of **392.92 ms** (mean **393.99 ms**, range **380.81–405.43 ms**).

The full solve returned objective **5,698,097,498.146622**, status **OPTIMAL**, and original-model KKT verification **PASS**. Residuals were primal **7.59e-9**, stationarity/dual **2.43e-16**, and complementarity **4.69e-16**. These are measured solver timings, not an artificially reduced display value.

## Website validation

The updated Python backend and actual website were exercised on a local Release CPU build. The QPLIB_9002 solve returned `OPTIMAL`, `Verification: PASS`, and the objective above. The website showed **396.2 ms** solver time, CPU backend, 43 Newton iterations, 43 linear solves, and 7,059 PCG iterations. Its end-to-end backend measurement was **1,574.1 ms**, which includes parsing, model preparation, presolve, solver, and verification work; browser rendering and network time are excluded. The website also displayed per-stage QP timing telemetry.

## Reference and limitations

An independent objective comparison was unavailable in this environment: the installed Gurobi restricted license rejected the 2,890-variable model as exceeding its size limit, and the attempted HiGHS QP run did not complete successfully. Therefore no independent objective-match claim is made. Objective convention was checked against QPLIB's specification, and objective-offset behavior has parser and C++ regression coverage.

The current QP path still supports diagonal Hessians only, convex minimization, and bounded variables; it does not yet provide general off-diagonal sparse Hessians, free variables, robust infeasibility certificates, or medium/large QP validation. This work does not claim production-grade general QP coverage or GPU acceleration.

## Native CLI acceptance run

The first direct CLI attempt exposed that C++ input dispatch did not recognize QPLIB and silently treated it as the simple TXT format. A native QPLIB reader was added for the supported diagonal-Hessian/linear-constraint subset; it preserves objective offsets, maps QPLIB infinity sentinels, translates ranged rows, and rejects unsupported off-diagonal Hessian entries. The generic CLI now reports the QP Hessian nonzero count and omits thousands of primal values by default (`--print-primal` displays them explicitly). Website files remain in the repository as an optional adapter; they are not part of the C++ executable's dependency path.

Final direct command: `cpp_solver/build-route-cpu/sovereign_presolve_cli.exe --input datasets/qp/small/QPLIB_9002.qplib --method qp --backend cpu`. Exact executable: `C:\Users\Apurva\Downloads\Gpu-main\Gpu-main\cpp_solver\build-route-cpu\sovereign_presolve_cli.exe`. The actual dataset parsed natively as 2,890 variables, 1,649 constraints, 9,690 linear nonzeros, and 2,890 diagonal Hessian nonzeros. Five sequential Release runs returned OPTIMAL and Verification PASS, with 43 Newton iterations each. Solver times: min **373.608 ms**, median **381.475 ms**, mean **381.278 ms**, max **387.918 ms**. Separate full process wall times (including startup, parsing, and captured output) were 548.712, 484.087, 469.944, 458.078, and 459.676 ms (median **469.944 ms**). The final objective and KKT residuals match the values recorded above.

Release configuration was MSVC + Ninja, `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG`. All 10 CTest C++ targets and all 59 Python tests passed. CLI smoke runs also solved `examples/afiro.mps` with revised simplex (`OPTIMAL`, verification PASS) and `examples/milp.txt` with branch-and-bound (`OPTIMAL`, verification PASS). The exact build invocation used the Visual Studio 2019 `VsDevCmd.bat -arch=x64` environment followed by the bundled Ninja executable targeting `sovereign_presolve_cli` and `sovereign_ipm_tests`.
