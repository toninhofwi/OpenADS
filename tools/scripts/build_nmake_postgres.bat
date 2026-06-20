@echo off
if not defined OPENADS_TOOLCHAIN_ROOT (
    echo ERROR: set OPENADS_TOOLCHAIN_ROOT to your MSVC toolchain root.
    exit /b 1
)
if not defined OPENADS_LIBPQ_INCLUDE (
    if exist "%OPENADS_TOOLCHAIN_ROOT%\pgsql\include\libpq-fe.h" (
        set "OPENADS_LIBPQ_INCLUDE=%OPENADS_TOOLCHAIN_ROOT%\pgsql\include"
    )
)
if not defined OPENADS_LIBPQ_LIBRARY (
    if exist "%OPENADS_TOOLCHAIN_ROOT%\libpq\x86\libpq.lib" (
        set "OPENADS_LIBPQ_LIBRARY=%OPENADS_TOOLCHAIN_ROOT%\libpq\x86\libpq.lib"
    )
)
call "%OPENADS_TOOLCHAIN_ROOT%\msvc\setup_x86.bat"
if exist "%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin" (
    set "PATH=%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin;%PATH%"
)
cd /d "%~dp0..\.."
set "EXTRA=-DOPENADS_WITH_POSTGRESQL=ON -DOPENADS_WITH_HTTP=OFF -DOPENADS_WARNINGS_AS_ERRORS=OFF"
if defined OPENADS_LIBPQ_INCLUDE set "EXTRA=%EXTRA% -DOPENADS_LIBPQ_INCLUDE=%OPENADS_LIBPQ_INCLUDE%"
if defined OPENADS_LIBPQ_LIBRARY set "EXTRA=%EXTRA% -DOPENADS_LIBPQ_LIBRARY=%OPENADS_LIBPQ_LIBRARY%"
cmake -S . -B build\pg -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release %EXTRA%
if errorlevel 1 exit /b 1
cmake --build build\pg
exit /b %ERRORLEVEL%