@echo off
rem build.cmd — build colonias_console.exe (headless remote smoke).
rem
rem Usage:
rem   build.cmd
rem   build.cmd C:\path\to\openads\build\default\src\Release
rem
rem Prereqs: Harbour 3.2 (%HB_INSTALL%, default c:\harbour) with
rem contrib/rddads for msvc64; MSVC x64 on PATH.

setlocal
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if "%HB_INSTALL%"=="" set "HB_INSTALL=c:\harbour"
set "OPENADS_LIB=%~1"
if "%OPENADS_LIB%"=="" set "OPENADS_LIB=%~dp0..\..\build\default\src\Release"
set "OPENADS_ROOT=%~dp0..\.."

if not exist "%HB_INSTALL%\contrib\rddads\ads.ch" (
   echo [hbmk2] ERROR: Harbour rddads not found at %HB_INSTALL%\contrib\rddads
   goto :err
)

set "HB_BIN=%HB_INSTALL%\bin\win\msvc64"
set "PATH=%HB_BIN%;%OPENADS_LIB%;%PATH%"

echo [hbmk2] colonias_console (OPENADS_LIB=%OPENADS_LIB%) ...
"%HB_BIN%\hbmk2.exe" -comp=msvc64 colonias_console.hbp || goto :err

echo [hbmk2] copying OpenADS ace64.dll ...
copy /y "%OPENADS_LIB%\openace64.dll" ace64.dll >nul 2>&1
if errorlevel 1 copy /y "%OPENADS_LIB%\ace64.dll" ace64.dll >nul 2>&1

echo [hbmk2] done. Run:  colonias_console.exe
endlocal & exit /b 0

:err
echo [hbmk2] BUILD FAILED (errorlevel %errorlevel%)
endlocal & exit /b 1