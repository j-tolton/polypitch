@echo off
setlocal
call C:\Development\emsdk\emsdk_env.bat
if exist build rmdir /s /q build
call emcmake cmake -S . -B build -G Ninja
if errorlevel 1 goto :error
cmake --build build
if errorlevel 1 goto :error
copy /Y build\polypitch.js web\
copy /Y build\polypitch.wasm web\
echo.
echo PolyPitch build complete.
exit /b 0
:error
echo.
echo BUILD FAILED
pause
exit /b 1
