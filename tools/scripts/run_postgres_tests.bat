@echo off
REM Run PostgreSQL ABI e2e tests from build\pg (or pass BUILD_DIR).
setlocal
set "ROOT=%~dp0..\.."
set "BUILD=%ROOT%\build\pg"
if not "%~1"=="" set "BUILD=%~1"

if not exist "%BUILD%\tests\openads_unit_tests.exe" (
    echo ERROR: %BUILD%\tests\openads_unit_tests.exe not found. Build first.
    exit /b 1
)

if not defined OPENADS_TOOLCHAIN_ROOT (
    if defined DEVAI_ROOT (
        set "OPENADS_TOOLCHAIN_ROOT=%DEVAI_ROOT%\_UtlAI"
    )
)
if not defined OPENADS_TOOLCHAIN_ROOT (
    echo ERROR: set OPENADS_TOOLCHAIN_ROOT or DEVAI_ROOT for libpq / winlibs PATH.
    exit /b 1
)

set "PATH=%OPENADS_TOOLCHAIN_ROOT%\pgsql\bin;%PATH%"
if exist "%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin" (
    set "PATH=%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin;%PATH%"
)

if not defined OPENADS_TEST_PG_URI (
    set "OPENADS_TEST_PG_URI=postgresql://postgres@127.0.0.1:5433/postgres"
)

cd /d "%BUILD%"
tests\openads_unit_tests.exe --test-case=*postgresql* %*
exit /b %ERRORLEVEL%