@echo off
rem Build a 64-bit Harbour toolchain from the cloned repository at f:\harbour64.
rem
rem Prerequisites:
rem   - Visual Studio 2022 Community installed
rem   - GNU make available on PATH as make or mingw32-make
rem   - Windows 64-bit host shell

setlocal
set HBROOT=f:\harbour64
set HB_WITH_ADS=F:\OpenADS\include\openads

if not exist "%HBROOT%\Makefile" (
    echo ERROR: Harbour repository not found at %HBROOT%
    goto :eof
)

if not exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: Visual Studio 2022 developer environment not found.
    echo Please install Visual Studio 2022 Community and rerun this script.
    goto :eof
)

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

rem Restore MSYS utilities (echo, sh, etc.) that vcvars may have pushed off PATH
set PATH=F:\MinGW63\msys\1.0\bin;%PATH%

set MAKECMD=
if exist "F:\MinGW63\msys\1.0\bin\make.exe" set MAKECMD=F:\MinGW63\msys\1.0\bin\make.exe
if "%MAKECMD%"=="" (
    where mingw32-make >nul 2>&1
    if not errorlevel 1 set MAKECMD=mingw32-make
)
if "%MAKECMD%"=="" (
    where make >nul 2>&1
    if not errorlevel 1 set MAKECMD=make
)

if "%MAKECMD%"=="" (
    echo ERROR: GNU make not found in PATH.
    echo Install MSYS2 or another GNU make distribution and ensure make or mingw32-make is available.
    echo For example, install MSYS2 and run: pacman -S make
    goto :eof
)

echo Using %MAKECMD% to build Harbour (msvc64)...
cd /d "%HBROOT%"
%MAKECMD% HB_COMPILER=msvc64
if errorlevel 1 (
    echo ERROR: Harbour build failed.
    goto :eof
)

echo Harbour build complete.  Built toolchain should be available under %HBROOT%\bin\win\msvc64
endlocal
