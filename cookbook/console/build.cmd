@echo off
rem ===================================================================
rem  Generic builder for the OpenADS cookbook -- console (pure Harbour)
rem  No drive letters, no machine-specific paths: everything comes from
rem  arguments / environment so anyone can run it as-is.
rem
rem  Usage:
rem     build.cmd <openads-lib-dir> [example.hbp]
rem
rem     <openads-lib-dir>  folder holding the OpenADS DLL + import lib
rem     [example.hbp]      which example to build (default: 01_hello_table.hbp)
rem
rem  Prerequisites:
rem     * a Visual Studio x64 "Developer Command Prompt" (so cl/link are on PATH)
rem     * hbmk2 on PATH, or set HB_BIN to its full path
rem     * OPENADS_ACELIB = import-lib base name (default: ace64; plus build: openace64)
rem ===================================================================
setlocal EnableDelayedExpansion

if "%~1"=="" (
   echo Usage: build.cmd ^<openads-lib-dir^> [example.hbp]
   exit /b 2
)

set "OPENADS_LIB=%~1"
if not defined OPENADS_ACELIB set "OPENADS_ACELIB=ace64"

set "HBP=%~2"
if "%HBP%"=="" set "HBP=01_hello_table.hbp"

set "HBMK=hbmk2"
if defined HB_BIN set "HBMK=%HB_BIN%"

rem Neutralize any inherited hbmk2 options (they can otherwise force a
rem 32-bit / wrong-compiler build) so this script is reproducible.
set "HBMK2="
set "HB_USER_OPTIONS="

rem Make sure the OpenADS DLL is found at run time, ahead of any other.
set "PATH=%OPENADS_LIB%;%PATH%"

pushd "%~dp0"

rem CRT-compat link flags: needed when Harbour's prebuilt libs were built
rem against an older Visual C++ runtime than the one on PATH (you'll see
rem unresolved __imp_modf / __imp_rand or LNK4217/LNK4286 without them).
rem Harmless when the runtimes already match. Drop them if your Harbour
rem build links cleanly on its own.
set "CRTFIX=-ldflag=/NODEFAULTLIB:msvcrt -ldflag=/NODEFAULTLIB:libucrt -ldflag=/NODEFAULTLIB:libvcruntime.lib -ldflag=/FORCE:MULTIPLE -lucrt -lvcruntime -llegacy_stdio_definitions"

echo === Building %HBP% ===
"%HBMK%" "%HBP%" -comp=msvc64 -cpu=x86_64 -static -gtstd %CRTFIX%
if errorlevel 1 (
   echo BUILD FAILED
   popd
   exit /b 1
)

rem Drop the OpenADS DLL next to the produced .exe so it loads first.
copy /y "%OPENADS_LIB%\%OPENADS_ACELIB%.dll" . >nul 2>&1

echo === Build OK ===
popd
endlocal
