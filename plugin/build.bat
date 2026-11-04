@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 goto :error
cmake --build build
if %errorlevel% neq 0 goto :error
echo.
echo PolyPitch plugin build complete: %~dp0build\PolyPitch_artefacts\Release\VST3\PolyPitch.vst3
exit /b 0
:error
echo.
echo BUILD FAILED. If it could not write PolyPitch.vst3, close Reaper and build again.
exit /b 1
