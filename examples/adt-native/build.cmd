@echo off
rem build.cmd — build adt_native_demo.exe (by glokcode)
rem Usage:
rem   build.cmd
rem   build.cmd C:\path\to\openads-release

setlocal
if "%HB_INSTALL%"=="" set "HB_INSTALL=c:\harbour"
set "OPENADS_LIB=%~1"
if "%OPENADS_LIB%"=="" set "OPENADS_LIB=%~dp0..\..\build\default\src\Release"

set "PATH=%HB_INSTALL%\bin\win\msvc64;%OPENADS_LIB%;%PATH%"

echo [hbmk2] adt_native_demo (OPENADS_LIB=%OPENADS_LIB%) ...
hbmk2 adt_native_demo.hbp || goto :err

copy /y "%OPENADS_LIB%\ace64.dll" . >nul 2>&1
echo [hbmk2] done. Run: adt_native_demo.exe
endlocal & exit /b 0

:err
echo [hbmk2] BUILD FAILED
endlocal & exit /b 1