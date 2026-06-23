@echo off
REM PostgreSQL backend build with MSVC cl (no winlibs GCC runtime at run time).
if not defined OPENADS_TOOLCHAIN_ROOT (
    echo ERROR: set OPENADS_TOOLCHAIN_ROOT to the directory that holds the
    echo        msvc, winlibs and pgsql dependency folders.
    exit /b 1
)
if not defined OPENADS_LIBPQ_INCLUDE (
    set "OPENADS_LIBPQ_INCLUDE=%OPENADS_TOOLCHAIN_ROOT%\pgsql\include"
)
if not defined OPENADS_LIBPQ_LIBRARY (
    set "OPENADS_LIBPQ_LIBRARY=%OPENADS_TOOLCHAIN_ROOT%\pgsql\lib\libpq.lib"
)
call "%OPENADS_TOOLCHAIN_ROOT%\msvc\setup_x64.bat"
if exist "%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin" (
    set "PATH=%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin;%PATH%"
)
cd /d "%~dp0..\.."
cmake -S . -B build\pg-msvc -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
    -DOPENADS_WITH_POSTGRESQL=ON -DOPENADS_WITH_HTTP=OFF ^
    -DOPENADS_WARNINGS_AS_ERRORS=OFF ^
    -DOPENADS_LIBPQ_INCLUDE=%OPENADS_LIBPQ_INCLUDE% ^
    -DOPENADS_LIBPQ_LIBRARY=%OPENADS_LIBPQ_LIBRARY%
if errorlevel 1 exit /b 1
cmake --build build\pg-msvc
exit /b %ERRORLEVEL%