@echo off
rem Timed NAV bench inside one exe run (connect + AdsOpenTable + SKIP per iter).
rem Usage: demo_nav_bench.bat [sqlite|odbc|pg|maria] [iters] [warmup] [oadll-src]
setlocal EnableExtensions
set "MODE=%~1"
if "%MODE%"=="" set "MODE=odbc"
set "ITERS=%~2"
if "%ITERS%"=="" set "ITERS=30"
set "WARMUP=%~3"
if "%WARMUP%"=="" set "WARMUP=1"
set "OADLL=%~4"
set "HERE=%~dp0"

set "OPENADS_NAV_BENCH_ITERS=%ITERS%"
set "OPENADS_NAV_BENCH_WARMUP=%WARMUP%"

call "%HERE%demo_nav_run.bat" %MODE% %OADLL%
exit /b %ERRORLEVEL%