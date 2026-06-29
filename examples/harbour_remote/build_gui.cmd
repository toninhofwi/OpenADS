@echo off
rem build_gui.cmd — build colonias.exe (FiveWin xBrowse GUI).
rem
rem Usage:
rem   build_gui.cmd
rem   build_gui.cmd C:\path\to\openads-release
rem
rem Prereqs: FiveWin (FWH) at %%FWDIR%% (default c:\fwteam), Harbour,
rem MSVC x64.

setlocal
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if "%HB_INSTALL%"=="" set "HB_INSTALL=c:\harbour"
if "%FWDIR%"=="" set "FWDIR=c:\fwteam"
set "OPENADS_LIB=%~1"
if "%OPENADS_LIB%"=="" set "OPENADS_LIB=%~dp0..\..\build\default\src\Release"
set "OPENADS_ROOT=%~dp0..\.."

set "HB_BIN=%HB_INSTALL%\bin\win\msvc64"
set "PATH=%HB_BIN%;%OPENADS_LIB%;%PATH%"

echo [hbmk2] colonias GUI (OPENADS_LIB=%OPENADS_LIB%, FWDIR=%FWDIR%) ...
"%HB_BIN%\hbmk2.exe" colonias_gui.hbp -ocolonias || goto :err

copy /y "%OPENADS_LIB%\openace64.dll" ace64.dll >nul 2>&1
if errorlevel 1 copy /y "%OPENADS_LIB%\ace64.dll" ace64.dll >nul 2>&1

echo [hbmk2] done. Run:  colonias.exe
endlocal & exit /b 0

:err
echo [hbmk2] BUILD FAILED (errorlevel %errorlevel%)
endlocal & exit /b 1