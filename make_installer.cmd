@echo off
rem make_installer.cmd  ->  dist\Clipper-Setup-<version>.exe
rem Release build in build-release, staged with only what the app needs, packed with Inno Setup 6.
setlocal
set "ROOT=%~dp0"
set "BUILD=%ROOT%build-release"
set "STAGE=%BUILD%\stage"
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo Inno Setup 6 non trovato: winget install JRSoftware.InnoSetup
  exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%BUILD%\build.ninja" (
  cmake -S "%ROOT%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build "%BUILD%" --target clipper || exit /b 1

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1
copy /y "%BUILD%\bin\clipper.exe" "%STAGE%\" >nul || exit /b 1
for %%L in (avcodec avformat avutil swscale swresample) do (
  copy /y "%BUILD%\bin\%%L-*.dll" "%STAGE%\" >nul || exit /b 1
)
mkdir "%STAGE%\licenses"
copy /y "%BUILD%\_deps\ffmpeg-src\LICENSE.txt" "%STAGE%\licenses\FFmpeg-LICENSE.txt" >nul
copy /y "%ROOT%installer\THIRD-PARTY.txt" "%STAGE%\licenses\" >nul

if not exist "%ROOT%dist" mkdir "%ROOT%dist"
"%ISCC%" /Q "/DStageDir=%STAGE%" "%ROOT%installer\clipper.iss" || exit /b 1
echo.
dir /b "%ROOT%dist\Clipper-Setup-*.exe"
