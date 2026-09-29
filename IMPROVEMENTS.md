# Sovereign Optimization Solver — Architectural Improvements & Technical Guide

**Optimization Engine Enhancements for SIH Benchmarking**  


---

## 1. Overview of Implemented Improvements

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

## 2. Detailed Technical Breakdown

### 2.1 Advanced Mixed-Integer Programming (`cpp_solver/include/milp/BranchAndBound.hpp`)

#### A. Pseudo-Cost Variable Scoring
- **Previous Bottleneck**: The solver used naive *first-fractional* variable branching, picking the first fractional index in variable order. This caused combinatorial explosion on MIPLIB models (e.g. 14,800+ nodes).
- **Implemented Upgrade**: Implemented **Pseudo-Cost & Fractional Closeness Scoring**:
  $$\mu_j^- = \frac{\sum \Delta z_j^-}{\sum f_j^-}, \quad \mu_j^+ = \frac{\sum \Delta z_j^+}{\sum f_j^+}$$
  $$score(j) = \max(\mu_j^- \cdot f_j, 10^{-6}) \times \max(\mu_j^+ \cdot (1 - f_j), 10^{-6}) + 0.1 \cdot (0.5 - |f_j - 0.5|)$$
  Variables that produce the steepest expected dual bound increase on both up and down branches are selected first.

#### B. Root Primal Heuristics
- At node 0, the solver automatically executes a bounded **Feasibility Pump** heuristic ($50$ projections/perturbations).
- If an integer-feasible solution is found, `incumbent` is initialized immediately before the first branch, enabling **aggressive branch pruning ($bound \ge incumbent$) across the entire tree**.

---

### 2.2 CUDA GPU Acceleration (`cpp_solver/build-cuda/sovereign_presolve_cli.exe`)

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

### 2.3 Native QPLIB Parser (`sovereign_solver/qplib.py`)

- Added dedicated native parser for `.qplib` text and AMPL format files from `qplib.zib.de`.
- Automatically extracts problem dimensions, variable types, linear objective coefficients, sparse quadratic terms $Q_{ij}$, and constraint bounds.
- Seamlessly integrates with the Newton-Barrier QP Interior Point solver.

---

## 3. How to Build & Run Updated Solvers

### 3.1 Build CPU Solver (GCC + Ninja)
```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","User") + ";" + [System.Environment]::GetEnvironmentVariable("Path","Machine")
cmake -S cpp_solver -B cpp_solver/build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build cpp_solver/build --config Release
```

### 3.2 Build GPU / CUDA Solver (NVCC 13.4 + MSVC)
```powershell
cmd /c scripts\build_cuda.bat
```

### 3.3 Verify GPU Device Detection
```powershell
.\cpp_solver\build-cuda\sovereign_presolve_cli.exe --device-info
```

---

## 4. Benchmark Execution Commands

```powershell
# Netlib LP Benchmark (CPU Revised Simplex vs HiGHS)
py -m sovereign_solver.benchmark --dataset netlib --input benchmarks/netlib/small --solver cpp_solver/build/sovereign_presolve_cli.exe --tier quick --compare-highs --compare-presolve --output-dir results/netlib

# Netlib LP Benchmark (GPU CUDA IPM on RTX 2050)
py -m sovereign_solver.benchmark --dataset netlib --input benchmarks/netlib/small --solver cpp_solver/build-cuda/sovereign_presolve_cli.exe --method ipm --backend cuda --compare-highs --output-dir results/netlib_cuda

# MIPLIB 2017 Benchmark (Enhanced Pseudo-Cost B&B vs HiGHS)
py -m sovereign_solver.benchmark --dataset miplib --input benchmarks/miplib/small --solver cpp_solver/build/sovereign_presolve_cli.exe --limit 10 --method milp --compare-highs --output-dir results/miplib

# QPLIB Benchmark (Newton Barrier Convex QP)
py -m sovereign_solver.benchmark --dataset qplib --input benchmarks/qplib --solver cpp_solver/build-cuda/sovereign_presolve_cli.exe --method qp --output-dir results/qplib
```
