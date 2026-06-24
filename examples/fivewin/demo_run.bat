@echo off
rem Build + run the multi-DB ADO bridge demo (x64, static CRT). Loads an
rem OpenADS openace64.dll built with a SQL-passthrough backend at runtime.
rem Set these first (see ../../tests/smoke/harbour/README.md):
rem   OPENADS_DLLDIR = dir with openace64.dll + openace64.lib (passthrough build)
rem   HB_BIN         = Harbour 64-bit hbmk2.exe
rem   MSVC_SETUP     = MSVC x64 environment .bat (optional if already in env)
rem   MSVC_REDIST    = dir with vcruntime140*.dll / msvcp140.dll (optional)
setlocal
set HERE=%~dp0
cd /d "%HERE%"
if not defined OPENADS_DLLDIR ( echo Set OPENADS_DLLDIR & exit /b 2 )
if not defined HB_BIN ( echo Set HB_BIN & exit /b 2 )

> demo_ado_multidb.hbp echo demo_ado_multidb.prg
>>demo_ado_multidb.hbp echo ../../tools/fwh_patch/openads_ado_bridge.prg
>>demo_ado_multidb.hbp echo -w3
>>demo_ado_multidb.hbp echo -I../../include
>>demo_ado_multidb.hbp echo -DOPENADS_ADO_NO_FWH
>>demo_ado_multidb.hbp echo -lrddads
>>demo_ado_multidb.hbp echo -L%OPENADS_DLLDIR%
>>demo_ado_multidb.hbp echo -lopenace64
>>demo_ado_multidb.hbp echo -ldflag=/NODEFAULTLIB:msvcrt
>>demo_ado_multidb.hbp echo -ldflag=/NODEFAULTLIB:libucrt
>>demo_ado_multidb.hbp echo -ldflag=/NODEFAULTLIB:libvcruntime.lib
>>demo_ado_multidb.hbp echo -ldflag=/FORCE:MULTIPLE
>>demo_ado_multidb.hbp echo -lucrt
>>demo_ado_multidb.hbp echo -lvcruntime
>>demo_ado_multidb.hbp echo -llegacy_stdio_definitions

set "HBMK2="
set "HB_USER_OPTIONS="
if defined MSVC_SETUP call "%MSVC_SETUP%" >nul 2>&1
if exist demo_ado_multidb.exe del demo_ado_multidb.exe

echo === BUILD ===
"%HB_BIN%" demo_ado_multidb.hbp -static -cpu=x86_64 -gtstd
if not exist demo_ado_multidb.exe ( echo BUILD_FAILED & exit /b 1 )

echo === PREP RUN ===
copy /y "%OPENADS_DLLDIR%\openace64.dll" . >nul
if defined MSVC_REDIST for %%D in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do if not exist %%D copy /y "%MSVC_REDIST%\%%D" . >nul 2>&1

echo === RUN ===
demo_ado_multidb.exe
echo exit=%ERRORLEVEL%
endlocal
