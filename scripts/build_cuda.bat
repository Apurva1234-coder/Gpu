@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0
set "PATH=C:\Program Files\CMake\bin;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;C:\Users\Devansh\AppData\Roaming\Python\Python314\Scripts;%PATH%"

if exist cpp_solver\build-cuda rmdir /s /q cpp_solver\build-cuda

cmake -S cpp_solver -B cpp_solver\build-cuda -G Ninja -DCMAKE_MAKE_PROGRAM="C:/Users/Devansh/AppData/Roaming/Python/Python314/Scripts/ninja.exe" -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl -DSOVEREIGN_ENABLE_CUDA=ON -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

cmake --build cpp_solver\build-cuda --config Release
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo === CUDA Build Succeeded ===
