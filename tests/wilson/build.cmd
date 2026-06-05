@echo off
rem Build the wilson NTX index test against Harbour's msvc64 rddads.lib +
rem OpenADS-built ace64.lib import library + ace64.dll on PATH.
rem
rem Usage: build.cmd [openads_build_dir]
rem   <openads_build_dir> defaults to ..\..\build\default\src\Release.
rem
rem Prerequisites:
rem   - Harbour 3.2+ with contrib/rddads installed at c:\harbour
rem   - OpenADS built (ace64.lib + ace64.dll)
rem   - Run from a Developer Command Prompt (or after vcvars64.bat)
rem     so hbmk2 can find cl.exe / link.exe.

setlocal
cd /d %~dp0
set HBROOT=c:\harbour
set OPENADS_LIB=%~1
if "%OPENADS_LIB%"=="" set OPENADS_LIB=%~dp0..\..\build\default\src\Release

set PATH=%HBROOT%\bin\win\msvc64;%OPENADS_LIB%;%PATH%

echo [wilson] hbmk2 build...
hbmk2 -comp=msvc64 -i"%HBROOT%\contrib\rddads" -lrddads -L"%OPENADS_LIB%" -lace64 test_open_ads_adsntx.prg

if %ERRORLEVEL% neq 0 (
    echo [wilson] BUILD FAILED
    exit /b 1
)

echo [wilson] Build OK. Run: test_open_ads_adsntx.exe
endlocal
