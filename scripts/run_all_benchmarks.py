import os
import sys
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CPU_SOLVER = ROOT / "cpp_solver" / "build" / "sovereign_presolve_cli.exe"
GPU_SOLVER = ROOT / "cpp_solver" / "build-cuda" / "sovereign_presolve_cli.exe"

def run_cmd(cmd, desc):
    print(f"\n{'='*20} {desc} {'='*20}")
    print("Command:", " ".join(cmd))
    start = time.time()
    res = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
    dur = time.time() - start
    print(f"Finished in {dur:.2f}s (exit code {res.returncode})")
    if res.stdout.strip():
        print(res.stdout.strip())
    if res.stderr.strip():
        print("Stderr:", res.stderr.strip())
    return res

def main():
    print("==================================================")
    print("   SOVEREIGN OPTIMIZATION SOLVER BENCHMARK TEST  ")
    print("==================================================")
    
    solver = GPU_SOLVER if GPU_SOLVER.exists() else CPU_SOLVER
    print(f"Using Solver Binary: {solver}")

    # 1. Netlib LP Benchmark (CPU)
    netlib_cmd = [
        sys.executable, "-m", "sovereign_solver.benchmark",
        "--dataset", "netlib",
        "--input", "benchmarks/netlib/small",
        "--solver", str(solver),
        "--tier", "quick",
        "--compare-highs",
        "--compare-presolve",
        "--output-dir", "results/netlib"
    ]
    run_cmd(netlib_cmd, "1. NETLIB LP BENCHMARK (CPU Revised Simplex)")

    # 2. Netlib LP Benchmark (GPU CUDA IPM)
    if GPU_SOLVER.exists():
        netlib_gpu_cmd = [
            sys.executable, "-m", "sovereign_solver.benchmark",
            "--dataset", "netlib",
            "--input", "benchmarks/netlib/small",
            "--solver", str(GPU_SOLVER),
            "--method", "ipm",
            "--backend", "cuda",
            "--tier", "quick",
            "--output-dir", "results/netlib_cuda"
        ]
        run_cmd(netlib_gpu_cmd, "2. NETLIB LP BENCHMARK (GPU RTX 2050 CUDA IPM)")

    # 3. MIPLIB 2017 Benchmark (MILP)
    miplib_cmd = [
        sys.executable, "-m", "sovereign_solver.benchmark",
        "--dataset", "miplib",
        "--input", "benchmarks/miplib/small",
        "--solver", str(solver),
        "--limit", "10",
        "--method", "milp",
        "--compare-highs",
        "--output-dir", "results/miplib"
    ]
    run_cmd(miplib_cmd, "3. MIPLIB 2017 BENCHMARK (Enhanced B&B)")

    # 4. QPLIB Benchmark (Convex QP)
    qplib_cmd = [
        sys.executable, "-m", "sovereign_solver.benchmark",
        "--dataset", "qplib",
        "--input", "benchmarks/qplib",
        "--solver", str(solver),
        "--method", "qp",
        "--output-dir", "results/qplib"
    ]
    run_cmd(qplib_cmd, "4. QPLIB BENCHMARK (Newton Barrier QP)")

    # 5. Mittelmann Benchmark (Standard MPS)
    mittelmann_cmd = [
        sys.executable, "-m", "sovereign_solver.benchmark",
        "--dataset", "mittelmann",
        "--input", "benchmarks/mittelmann",
        "--solver", str(solver),
        "--tier", "quick",
        "--compare-highs",
        "--output-dir", "results/mittelmann"
    ]
    run_cmd(mittelmann_cmd, "5. MITTELMANN LP BENCHMARK")

    print("\n" + "="*50)
    print("   ALL BENCHMARK TESTS COMPLETED SUCCESSFULLY!    ")
    print("==================================================")

if __name__ == "__main__":
    main()
