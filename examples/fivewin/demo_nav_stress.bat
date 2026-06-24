@echo off
rem Repeat NAV smoke N times (default 5). Args: [mode] [repeats] [oadll-src]
setlocal EnableExtensions
set "MODE=%~1"
if "%MODE%"=="" set "MODE=odbc"
set "N=%~2"
if "%N%"=="" set "N=5"
set "OADLL=%~3"
set "HERE=%~dp0"
set "FAIL=0"

echo === NAV stress: %N% runs mode=%MODE% ===
for /L %%I in (1,1,%N%) do (
  echo.
  echo --- run %%I / %N% ---
  call "%HERE%demo_nav_run.bat" %MODE% %OADLL%
  if errorlevel 1 set "FAIL=1"
)
if "%FAIL%"=="1" (
  echo STRESS_FAIL
  exit /b 1
)
echo STRESS_OK %N%/%N%
exit /b 0