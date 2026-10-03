param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8000
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

$listenerPattern = ":$Port\s+\S+\s+LISTENING\s+(\d+)$"
$listeners = @(netstat -ano -p tcp | ForEach-Object {
    if ($_ -match $listenerPattern) { [pscustomobject]@{ OwningProcess = [int]$Matches[1] } }
} | Sort-Object OwningProcess -Unique)
if ($listeners.Count -gt 0) {
    $pids = ($listeners | Select-Object -ExpandProperty OwningProcess -Unique) -join ", "
    try {
        $health = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/health" -TimeoutSec 3
        if ($health.status -eq "ready") {
            Write-Host "Sovereign demo is already responding at http://127.0.0.1:$Port (PID $pids). No second server was started."
            exit 0
        }
    } catch {
        # The port is occupied by a process that is not a healthy Sovereign API.
    }
    throw "Port $Port is already occupied by PID $pids. Inspect that process and stop it before starting the demo."
}

$SolverOverride = $env:SOVEREIGN_SOLVER_EXECUTABLE
if ($SolverOverride) {
    $SolverPath = if ([System.IO.Path]::IsPathRooted($SolverOverride)) { $SolverOverride } else { Join-Path $RepoRoot $SolverOverride }
    if (-not (Test-Path -LiteralPath $SolverPath -PathType Leaf)) {
        throw "SOVEREIGN_SOLVER_EXECUTABLE does not point to a file: $SolverPath"
    }
} else {
    $SolverPath = Join-Path $RepoRoot "cpp_solver\build-route-cpu\sovereign_presolve_cli.exe"
    if (-not (Test-Path -LiteralPath $SolverPath -PathType Leaf)) {
        throw "Release CLI not found at $SolverPath. Build cpp_solver first or set SOVEREIGN_SOLVER_EXECUTABLE."
    }
}

$Python = Join-Path $RepoRoot ".venv-web\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    $PythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if (-not $PythonCommand) { throw "Python was not found. Create .venv-web or install Python and add it to PATH." }
    $Python = $PythonCommand.Source
}
& $Python -c "import fastapi, uvicorn, multipart"
if ($LASTEXITCODE -ne 0) {
    throw "Web dependencies are missing. Run: $Python -m pip install -r webui\requirements.txt"
}

$env:SOVEREIGN_SOLVER_EXECUTABLE = $SolverPath
Write-Host "Native Sovereign CLI: $SolverPath"
Write-Host "Serving UI and API together at http://127.0.0.1:$Port"
Write-Host "Press Ctrl+C to stop this single demo server."
& $Python -m uvicorn webui.app:app --host 127.0.0.1 --port $Port
exit $LASTEXITCODE
