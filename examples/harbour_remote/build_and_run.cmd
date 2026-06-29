@echo off
rem build_and_run.cmd — build, start openads_serverd, run headless smoke.
rem
rem Usage:
rem   build_and_run.cmd
rem   build_and_run.cmd C:\path\to\openads-release

setlocal
set "OPENADS_LIB=%~1"
if "%OPENADS_LIB%"=="" set "OPENADS_LIB=%~dp0..\..\build\default\src\Release"
set "OPENADS_SERVERD=%~dp0..\..\build\default\tools\serverd\Release\openads_serverd.exe"
set "DATA_DIR=%~dp0data"
set "PORT=6262"

cd /d "%~dp0"

call build.cmd "%OPENADS_LIB%" || exit /b 1

taskkill /IM openads_serverd.exe /F >nul 2>&1

if not exist "%OPENADS_SERVERD%" (
   echo ERROR: openads_serverd not found at %OPENADS_SERVERD%
   endlocal & exit /b 1
)

echo === Starting OpenADS server on port %PORT% ===
start /b "" "%OPENADS_SERVERD%" --port %PORT% --data "%DATA_DIR%"
ping 127.0.0.1 -n 10 >nul

set "OADS_REMOTE_URI=tcp://127.0.0.1:%PORT%/"
echo === Running colonias_console.exe ===
colonias_console.exe >smoke_last.log 2>&1
type smoke_last.log
if errorlevel 1 (
   taskkill /IM openads_serverd.exe /F >nul 2>&1
   echo RUN FAILED
   endlocal & exit /b 1
)

taskkill /IM openads_serverd.exe /F >nul 2>&1

echo === Smoke test OK ===
endlocal & exit /b 0