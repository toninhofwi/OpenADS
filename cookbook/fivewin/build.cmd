@echo off
rem ===================================================================
rem  Builder for the OpenADS cookbook -- FiveWin (GUI) example.
rem  No drive letters: everything comes from arguments / environment.
rem
rem  Usage:
rem     build.cmd <openads-lib-dir> [example.hbp]
rem        <openads-lib-dir>  folder with the OpenADS DLL + import lib
rem        [example.hbp]      default: crud_browse.hbp
rem
rem  Environment (set to YOUR installs before running):
rem     HB_INSTALL     Harbour root (holds bin\hbmk2.exe)
rem     FWDIR64        FiveWin install (include\ + lib\FiveH64.lib)
rem     MSVC_UCRT_X64  Windows SDK ucrt x64 lib dir
rem     MSVC_SETUP     a vcvars/x64 env .bat (so cl/link are on PATH)
rem     OPENADS_ACELIB import-lib base name (default openace64)
rem
rem  After building, run with /auto for a head-less CRUD smoke (no GUI):
rem     crud_browse.exe /auto
rem ===================================================================
setlocal EnableDelayedExpansion

if "%~1"=="" ( echo Usage: build.cmd ^<openads-lib-dir^> [example.hbp] & exit /b 2 )
set "OPENADS_LIB=%~1"
set "HBP=%~2"
if "%HBP%"=="" set "HBP=crud_browse.hbp"
for %%F in ("%HBP%") do set "OUT=%%~nF"

if not defined OPENADS_ACELIB set "OPENADS_ACELIB=openace64"
if not defined HB_INSTALL ( echo Set HB_INSTALL to your Harbour root & exit /b 2 )
if not defined FWDIR64 ( echo Set FWDIR64 to your FiveWin install & exit /b 2 )

rem Neutralize inherited hbmk2 options (they can force a 32-bit build).
set "HBMK2="
set "HB_USER_OPTIONS="
set "PATH=%OPENADS_LIB%;%PATH%"

if defined MSVC_SETUP call "%MSVC_SETUP%" >nul 2>&1

pushd "%~dp0"
echo === Building %HBP% (FiveWin 64-bit) ===
"%HB_INSTALL%\bin\hbmk2.exe" "%HBP%" -static -cpu=x86_64 -o%OUT%
if errorlevel 1 ( echo BUILD FAILED & popd & exit /b 1 )

copy /y "%OPENADS_LIB%\%OPENADS_ACELIB%.dll" . >nul 2>&1
echo === Build OK : %OUT%.exe ===
popd
endlocal
