@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build\build.ninja (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
)
cmake --build build || exit /b 1
if "%1"=="test" (
  build\bin\clipper_tests.exe %2 %3 %4 %5 || exit /b 1
)
