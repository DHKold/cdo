@echo off
REM bootstrap.cmd — Bootstrap CDo from source (Windows CMD)
REM
REM Requires: gcc on PATH or .cdo\tools\w64devkit\bin\gcc.exe
REM

setlocal enabledelayedexpansion

echo.
echo   CDo Bootstrap
echo   =============
echo.

REM Check for compiler
where gcc >nul 2>&1
if %errorlevel%==0 (
    echo Found gcc on PATH
    goto :compile
)

if exist ".cdo\tools\w64devkit\bin\gcc.exe" (
    echo Using vendored gcc from .cdo\tools\w64devkit\bin\
    set "PATH=.cdo\tools\w64devkit\bin;%PATH%"
    goto :compile
)

echo ERROR: No C compiler found.
echo Run bootstrap.ps1 (PowerShell) to auto-download w64devkit,
echo or install GCC/MinGW manually and add it to PATH.
exit /b 1

:compile
if not exist "build\bootstrap" mkdir "build\bootstrap"

echo Compiling sources...
set OBJ_FILES=

for /r "src\cdo" %%f in (*.c) do (
    set "SRC=%%f"
    set "REL=%%~nf"
    gcc -c -std=c17 -Isrc\cdo -o "build\bootstrap\!REL!.o" "%%f"
    if !errorlevel! neq 0 (
        echo ERROR: Failed to compile %%f
        exit /b 1
    )
    set "OBJ_FILES=!OBJ_FILES! build\bootstrap\!REL!.o"
)

for /r "src\cdo" %%f in (*.cpp) do (
    set "SRC=%%f"
    set "REL=%%~nf"
    g++ -c -std=c++20 -Isrc\cdo -o "build\bootstrap\!REL!.o" "%%f"
    if !errorlevel! neq 0 (
        echo ERROR: Failed to compile %%f
        exit /b 1
    )
    set "OBJ_FILES=!OBJ_FILES! build\bootstrap\!REL!.o"
)

echo Linking...
g++ !OBJ_FILES! -o cdo.exe -lwinhttp -static
if %errorlevel% neq 0 (
    echo ERROR: Linking failed
    exit /b 1
)

echo.
echo   Bootstrap complete!
echo   Binary: cdo.exe
echo.
echo   You can now use: cdo.exe build cdo
echo.
