@echo off
rem Build SAP and OpenADS engine bench exes (64-bit Harbour + rddads).
setlocal EnableExtensions
set "HERE=%~dp0"
set "BIN=%HERE%bin"
rem Toolchain locations come from the environment (no hardcoded install paths):
rem   OPENADS_HB_BIN     - hbmk2(.exe) (default: hbmk2 on PATH)
rem   OPENADS_MSVC_SETUP - MSVC x64 env setup .bat (optional)
rem   MSVC_UCRT_X64      - UCRT x64 lib dir (optional)
rem   HB_INSTALL         - Harbour root (used to derive ACE64_LIB_PATH)
rem   ACE64_LIB_PATH     - dir with ace64.lib  /  OPENACE64_LIB - openace64.lib
if not defined OPENADS_HB_BIN set "OPENADS_HB_BIN=hbmk2"
if not defined OPENADS_MSVC_UCRT if defined MSVC_UCRT_X64 set "OPENADS_MSVC_UCRT=%MSVC_UCRT_X64%"
if not defined ACE64_LIB_PATH if defined HB_INSTALL (
  set "ACE64_LIB_PATH=%HB_INSTALL%\lib"
)
if not exist "%BIN%" mkdir "%BIN%"
cd /d "%HERE%"

if not defined OPENACE64_LIB (
  echo [ERRO] defina OPENACE64_LIB
  exit /b 1
)
if not defined ACE64_LIB_PATH (
  echo [ERRO] defina ACE64_LIB_PATH
  exit /b 1
)
if defined OPENADS_MSVC_SETUP call "%OPENADS_MSVC_SETUP%" >nul 2>&1
set "HBMK2="
set "HB_USER_OPTIONS="

echo [build] openads...
"%OPENADS_HB_BIN%" ads_engine_openads.hbp -static -comp=msvc -cpu=x86_64 -gtstd -o"%BIN%\ads_engine_openads_64"
if not exist "%BIN%\ads_engine_openads_64.exe" (
  echo BUILD_FAILED openads
  exit /b 1
)
echo BUILD_OK %BIN%\ads_engine_openads_64.exe

echo [build] sap...
"%OPENADS_HB_BIN%" ads_engine_sap.hbp -static -comp=msvc -cpu=x86_64 -gtstd -o"%BIN%\ads_engine_sap_64"
if not exist "%BIN%\ads_engine_sap_64.exe" (
  echo BUILD_FAILED sap
  exit /b 1
)
echo BUILD_OK %BIN%\ads_engine_sap_64.exe

echo [build] prepare...
"%OPENADS_HB_BIN%" ads_prepare_sap.hbp -static -comp=msvc -cpu=x86_64 -gtstd -o"%BIN%\ads_prepare_sap_64"
if not exist "%BIN%\ads_prepare_sap_64.exe" (
  echo BUILD_FAILED prepare
  exit /b 1
)
echo BUILD_OK %BIN%\ads_prepare_sap_64.exe
exit /b 0