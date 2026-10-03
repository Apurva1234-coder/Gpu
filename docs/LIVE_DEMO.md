# Sovereign live demo

The dashboard is a presentation layer over the native Sovereign CLI. It does not contain a second optimization implementation. The Solve page runs the selected repository dataset through the CLI; the Reports page reads saved JSON benchmark artifacts and does not rerun the suite.

## Requirements

- Windows PowerShell
- CMake-built Release CLI at `cpp_solver/build-route-cpu/sovereign_presolve_cli.exe`
- Python dependencies installed in `.venv-web` (`python -m pip install -r webui\requirements.txt`)

## Start

From the repository root, open one PowerShell terminal and run:

```powershell
.\scripts\start_live_demo.ps1
```

The FastAPI process serves both the frontend and API from one origin:

- Frontend: <http://127.0.0.1:8000/>
- API health: <http://127.0.0.1:8000/api/health>
- API docs: <http://127.0.0.1:8000/docs>

Port 8000 is canonical. The startup script detects an existing healthy instance and avoids starting a duplicate. To use another port intentionally, pass `-Port 8001`; the UI uses same-origin API URLs automatically.

The CLI is selected relative to the repository by default. To select a different build explicitly for that PowerShell session:

```powershell
$env:SOVEREIGN_SOLVER_EXECUTABLE = "cpp_solver\build-route-cpu\sovereign_presolve_cli.exe"
```

For a CUDA-capable executable, set `SOVEREIGN_CUDA_SOLVER_EXECUTABLE` to its repository-relative or absolute path. CUDA is reported as used only when the native CLI reports that the particular run used it. The QP Newton path and MILP branch-and-bound currently use CPU; selecting CUDA for an unsupported method returns a clear error.

## Rehearsal sequence

1. Open the dashboard and select **Solve**.
2. In the built-in dataset cards, select LP → **AFIRO**, then solve. Check the live objective, status, and verification.
3. Select MILP → **FLUGPL**, then solve. Check original-model verification, nodes, LP solves/iterations, incumbent, bound, and gap.
4. Select QP → **QPLIB_9002**, then solve. Check objective, Newton iterations, KKT verification, and residuals.
5. Open **Benchmarks**. The “Final solver measurements” table is read from the checked-in final benchmark JSON files; it is stored evidence, not a new solve. Older per-suite reports remain available below it.
6. To rehearse restart, stop the foreground server with **Ctrl+C**, rerun the same command, and solve one model again.

The native CLI remains available independently for checking a website result:

```powershell
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\lp\small\afiro.mps --method revised-simplex --backend cpu
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\milp\small\flugpl.mps --method milp --backend cpu
cpp_solver\build-route-cpu\sovereign_presolve_cli.exe --input datasets\qp\small\QPLIB_9002.qplib --method qp --backend cpu
```

## Troubleshooting

- **Port 8000 is occupied:** inspect it with `Get-NetTCPConnection -State Listen -LocalPort 8000 | Select-Object LocalAddress,LocalPort,OwningProcess`. Check the owning process before stopping it. Restart the demo script after freeing the port.
- **Backend unavailable:** check `/api/health`, confirm the CLI path exists, and rebuild the Release target with `cmake --build cpp_solver/build-route-cpu --config Release`.
- **Dataset unavailable:** confirm the corresponding file exists under `datasets/lp/small`, `datasets/milp/small`, or `datasets/qp/small`.
- **CUDA unavailable:** CPU is the supported fallback for AFIRO, FLUGPL, and QPLIB_9002. The UI shows the backend reported by the completed native run; it does not infer GPU use from a detected GPU.
- **A solve is already running:** the API allows one active native solve at a time. Wait for it to finish or cancel it in the UI before starting another.

The report records identify their source JSON and the revision measured. Timings on this page are historical solver timings; live browser and HTTP latency are not reported as solver time.
