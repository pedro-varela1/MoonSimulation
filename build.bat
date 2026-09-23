@echo off
set PATH=C:\msys64\ucrt64\bin;%PATH%
mingw32-make -j8 || exit /b 1
if "%1"=="run" MoonSimulation.exe
