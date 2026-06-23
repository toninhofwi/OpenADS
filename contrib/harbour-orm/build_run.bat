@echo off
rem Portable build+run for the ORM smoke. Set first:
rem   HB_BIN          = 64-bit hbmk2.exe
rem   OPENADS_DLLDIR  = dir with openace64.dll + openace64.lib (SQL-passthrough build)
rem   OPENADS_INCDIR  = dir holding openads\ace.h (for the C glue BEGINDUMP)
rem   MSVC_SETUP      = MSVC x64 env .bat (optional if already in env)
setlocal
set HERE=%~dp0
cd /d "%HERE%"
if not defined HB_BIN ( echo Set HB_BIN & exit /b 2 )
if not defined OPENADS_DLLDIR ( echo Set OPENADS_DLLDIR & exit /b 2 )
if not defined OPENADS_INCDIR ( echo Set OPENADS_INCDIR & exit /b 2 )

> orm_smoke.hbp echo tests/smoke.prg
>>orm_smoke.hbp echo tests/_assert.prg
>>orm_smoke.hbp echo src/hbo_ace.prg
if exist src\connection.prg   >>orm_smoke.hbp echo src/connection.prg
if exist src\grammar.prg      >>orm_smoke.hbp echo src/grammar.prg
if exist src\querybuilder.prg >>orm_smoke.hbp echo src/querybuilder.prg
if exist src\schema.prg       >>orm_smoke.hbp echo src/schema.prg
if exist src\model.prg        >>orm_smoke.hbp echo src/model.prg
>>orm_smoke.hbp echo -oorm_smoke
>>orm_smoke.hbp echo -w3
>>orm_smoke.hbp echo -Iinclude
>>orm_smoke.hbp echo -I%OPENADS_INCDIR%
>>orm_smoke.hbp echo -lrddads
>>orm_smoke.hbp echo -L%OPENADS_DLLDIR%
>>orm_smoke.hbp echo -lopenace64
>>orm_smoke.hbp echo -ldflag=/NODEFAULTLIB:msvcrt
>>orm_smoke.hbp echo -ldflag=/NODEFAULTLIB:libucrt
>>orm_smoke.hbp echo -ldflag=/NODEFAULTLIB:libvcruntime.lib
>>orm_smoke.hbp echo -ldflag=/FORCE:MULTIPLE
>>orm_smoke.hbp echo -lucrt
>>orm_smoke.hbp echo -lvcruntime
>>orm_smoke.hbp echo -llegacy_stdio_definitions

if defined MSVC_SETUP call "%MSVC_SETUP%" >nul 2>&1
rem Neutralize leaked hbmk2 env from the "menu Y" CMD (forces cpu=x86 otherwise).
set "HBMK2="
set "HB_USER_OPTIONS="
if exist orm_smoke.exe del orm_smoke.exe
if exist orm_smoke.db  del orm_smoke.db

echo === BUILD ===
"%HB_BIN%" orm_smoke.hbp -static -cpu=x86_64 -gtstd
if not exist orm_smoke.exe ( echo BUILD_FAILED & exit /b 1 )

echo === RUN ===
copy /y "%OPENADS_DLLDIR%\openace64.dll" . >nul
.\orm_smoke.exe
echo exit=%ERRORLEVEL%
endlocal
