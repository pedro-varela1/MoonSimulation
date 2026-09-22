@echo off
REM Build do MoonSimulation com o toolchain MinGW do MSYS2 (UCRT64).
REM Requer: pacman -S mingw-w64-ucrt-x86_64-freeglut mingw-w64-ucrt-x86_64-glew

set PATH=C:\msys64\ucrt64\bin;%PATH%

g++ -DWIN32 -std=gnu++17 -O2 -w -I. ^
    main.cpp sphere.cpp bmpToTexture.cpp tifReader.cpp glslprogram.cpp ^
    -lfreeglut -lglew32 -lopengl32 -lglu32 ^
    -o MoonSimulation.exe

if errorlevel 1 (
    echo.
    echo BUILD FALHOU.
    exit /b 1
)

echo Build OK.
if "%1"=="run" MoonSimulation.exe
