@echo off
setlocal

rem Builds the optional CUDA solver without modifying the working CPU build.
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
set "CUDA_ROOT=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1"
set "HOST_COMPILER=%VSROOT%\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64"

if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
  echo MSVC Build Tools 2019 were not found.
  exit /b 1
)
if not exist "%CUDA_ROOT%\bin\nvcc.exe" (
  echo CUDA Toolkit 13.1 was not found.
  exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if not exist cpp_solver\build-cuda mkdir cpp_solver\build-cuda
"%CUDA_ROOT%\bin\nvcc.exe" -std=c++17 -O2 -DNDEBUG -allow-unsupported-compiler -ccbin "%HOST_COMPILER%" -DSOVEREIGN_HAS_CUDA=1 -I cpp_solver\include cpp_solver\src\main.cpp cpp_solver\cuda\CudaBackend.cu -o cpp_solver\build-cuda\sovereign_presolve_cli.exe -lcublas -lcusparse -lcusolver
if errorlevel 1 exit /b 1

echo CUDA solver built: cpp_solver\build-cuda\sovereign_presolve_cli.exe
