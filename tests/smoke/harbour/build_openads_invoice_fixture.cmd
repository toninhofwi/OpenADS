@echo off
rem Build the openads_cdx_invoice_fixture sample with the newly compiled
rem Harbour 64-bit toolchain and OpenADS.

setlocal
set HBROOT=f:\harbour64
set OPENADS_LIB=F:\OpenADS\build\msvc-x64\src\Release

if not exist "%HBROOT%\bin\win\msvc64\hbmk2.exe" (
    echo ERROR: Harbour binary not found at %HBROOT%\bin\win\msvc64\hbmk2.exe
    echo Build Harbour first with build_harbour64.cmd
    goto :eof
)

if not exist "%OPENADS_LIB%\openace64.lib" (
    echo ERROR: OpenADS lib directory not found at %OPENADS_LIB%
    goto :eof
)

if not exist "openads_cdx_invoice_fixture.prg" (
    echo ERROR: Sample source openads_cdx_invoice_fixture.prg not found in the current folder.
    goto :eof
)

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH=%HBROOT%\bin\win\msvc64;%OPENADS_LIB%;%PATH%

"%HBROOT%\bin\win\msvc64\hbmk2.exe" -i"%HBROOT%\contrib\rddads" -lrddads -L"%OPENADS_LIB%" -lopenace64 openads_cdx_invoice_fixture.prg
if errorlevel 1 (
    echo ERROR: Sample build failed.
    goto :eof
)

echo Build succeeded.
endlocal
