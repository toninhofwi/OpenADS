@echo off
:: OpenADS Windows build script
:: Requires: Visual Studio 2022 (any edition)
::
:: Outputs:
::   build\msvc-x64\src\Release\openace64.dll    OpenADS ACE-compatible DLL
::   build\msvc-x64\Release\openads_serverd.exe   standalone TCP server
::   build\msvc-x64\Release\openads_bench.exe     SQL benchmark tool
::
:: Options (pass as arguments):
::   debug      - build Debug instead of Release
::   notls      - skip TLS support
::   nohttp     - skip embedded web console (faster configure, no FetchContent)
::   tests      - also run the test suite after building

setlocal
set BUILD_TYPE=Release
set WITH_TLS=OFF
set WITH_HTTP=ON
set RUN_TESTS=0

for %%A in (%*) do (
    if /I "%%A"=="debug"  set BUILD_TYPE=Debug
    if /I "%%A"=="notls"  set WITH_TLS=OFF
    if /I "%%A"=="nohttp" set WITH_HTTP=OFF
    if /I "%%A"=="tests"  set RUN_TESTS=1
)

:: Use CMake bundled with VS2022 if cmake is not on PATH
where cmake >nul 2>&1
if errorlevel 1 (
    set "PATH=%PATH%;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
)
cmake --version

echo.
echo === Configuring (msvc-x64, %BUILD_TYPE%) ===
cmake --preset msvc-x64 ^
    -DOPENADS_WITH_TLS=%WITH_TLS% ^
    -DOPENADS_WITH_HTTP=%WITH_HTTP% ^
    -DOPENADS_WARNINGS_AS_ERRORS=OFF
if errorlevel 1 goto :fail

echo.
echo === Building ===
cmake --build build/msvc-x64 --config %BUILD_TYPE% --parallel
if errorlevel 1 goto :fail

echo.
echo === Build complete ===
echo   DLL : build\msvc-x64\src\%BUILD_TYPE%\openace64.dll
echo   Srv : build\msvc-x64\%BUILD_TYPE%\openads_serverd.exe
echo   Bench: build\msvc-x64\%BUILD_TYPE%\openads_bench.exe

if %RUN_TESTS%==1 (
    echo.
    echo === Running tests ===
    ctest --test-dir build/msvc-x64 --output-on-failure -C %BUILD_TYPE%
)

endlocal
exit /b 0

:fail
echo.
echo === BUILD FAILED ===
endlocal
exit /b 1
