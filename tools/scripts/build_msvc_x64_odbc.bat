@echo off
REM Build OpenADS x64 with the ODBC Plus backend (MSVC).
REM Prereq: Visual Studio x64 tools on PATH (Developer Command Prompt).
REM The ODBC driver-manager import library (odbc32) ships with the
REM Windows SDK, so there is no external dependency to configure.
cd /d "%~dp0..\.."
cmake -S . -B build\odbc-msvc -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
    -DOPENADS_WITH_ODBC=ON -DOPENADS_WITH_HTTP=OFF -DOPENADS_WITH_TLS=OFF ^
    -DOPENADS_WARNINGS_AS_ERRORS=OFF
if errorlevel 1 exit /b 1
cmake --build build\odbc-msvc
exit /b %ERRORLEVEL%
