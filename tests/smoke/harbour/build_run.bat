@echo off
rem Build + run the ADO bridge console smoke (x64, static CRT). Loads an
rem OpenADS openace64.dll built with a SQL-passthrough backend at runtime.
rem Set these first (see README.md):
rem   OPENADS_DLLDIR = dir with openace64.dll + openace64.lib (passthrough build)
rem   HB_BIN         = Harbour 64-bit hbmk2.exe
rem   MSVC_SETUP     = MSVC x64 environment .bat (optional if already in env)
rem   MSVC_REDIST    = dir with vcruntime140*.dll / msvcp140.dll (optional)
setlocal
set HERE=%~dp0
cd /d "%HERE%"
if not defined OPENADS_DLLDIR ( echo Set OPENADS_DLLDIR ^(see README.md^) & exit /b 2 )
if not defined HB_BIN ( echo Set HB_BIN ^(see README.md^) & exit /b 2 )

> ado_class_smoke.hbp echo ado_class_smoke.prg
>>ado_class_smoke.hbp echo ../../../tools/fwh_patch/openads_ado_bridge.prg
>>ado_class_smoke.hbp echo -w3
>>ado_class_smoke.hbp echo -I../../../include
>>ado_class_smoke.hbp echo -DOPENADS_ADO_NO_FWH
>>ado_class_smoke.hbp echo -lrddads
>>ado_class_smoke.hbp echo -L%OPENADS_DLLDIR%
>>ado_class_smoke.hbp echo -lopenace64
>>ado_class_smoke.hbp echo -ldflag=/NODEFAULTLIB:msvcrt
>>ado_class_smoke.hbp echo -ldflag=/NODEFAULTLIB:libucrt
>>ado_class_smoke.hbp echo -ldflag=/NODEFAULTLIB:libvcruntime.lib
>>ado_class_smoke.hbp echo -ldflag=/FORCE:MULTIPLE
>>ado_class_smoke.hbp echo -lucrt
>>ado_class_smoke.hbp echo -lvcruntime
>>ado_class_smoke.hbp echo -llegacy_stdio_definitions

set "HBMK2="
set "HB_USER_OPTIONS="
if defined MSVC_SETUP call "%MSVC_SETUP%" >nul 2>&1
if exist ado_class_smoke.exe del ado_class_smoke.exe

echo === BUILD ===
"%HB_BIN%" ado_class_smoke.hbp -static -cpu=x86_64 -gtstd
if not exist ado_class_smoke.exe ( echo BUILD_FAILED & exit /b 1 )

echo === PREP RUN ===
copy /y "%OPENADS_DLLDIR%\openace64.dll" . >nul
if defined MSVC_REDIST for %%D in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do if not exist %%D copy /y "%MSVC_REDIST%\%%D" . >nul 2>&1
if not defined ADO_SMOKE_URI set ADO_SMOKE_URI=sqlite://ado_smoke.db

echo === RUN ===
ado_class_smoke.exe
echo exit=%ERRORLEVEL%
endlocal
