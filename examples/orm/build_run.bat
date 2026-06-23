@echo off
rem build_run.bat -- portable build + run for the ORM demo over the OpenADS ACE ABI.
rem
rem Required env vars (set before calling this script):
rem   HB_BIN         -- path to 64-bit hbmk2.exe
rem                     e.g. C:\harbour64\bin\hbmk2.exe
rem   OPENADS_LIB    -- directory containing ace64.dll + ace64.lib (Release build)
rem                     e.g. C:\openads-release
rem   OPENADS_INC    -- directory holding openads\ace.h
rem                     e.g. C:\openads-src\include
rem   MSVC_SETUP     -- (optional) path to MSVC x64 env script, e.g. vcvars64.bat
rem                     skip if already in a Developer Command Prompt
rem
rem Connection URI used in the demo: sqlite://orm_ace_demo.db
rem   Every call lands on ace64.dll (SQL passthrough backend).
rem   To use a DBF table set: dbf://<directory>
rem   To use a server:        tcp://<host>:<port>/<database>
setlocal
set HERE=%~dp0
cd /d "%HERE%"
if not defined HB_BIN        ( echo Set HB_BIN        & exit /b 2 )
if not defined OPENADS_LIB   ( echo Set OPENADS_LIB   & exit /b 2 )
if not defined OPENADS_INC   ( echo Set OPENADS_INC   & exit /b 2 )

rem Neutralize any inherited hbmk2 env (can otherwise force cpu=x86).
set "HBMK2="
set "HB_USER_OPTIONS="

rem Apply MSVC x64 env if provided and not already applied.
if defined MSVC_SETUP call "%MSVC_SETUP%" >nul 2>&1

rem Generate the .hbp on every run so paths stay current.
> orm_ace_demo.hbp echo demo.prg
>>orm_ace_demo.hbp echo src/connection.prg
>>orm_ace_demo.hbp echo src/grammar.prg
>>orm_ace_demo.hbp echo src/blueprint.prg
>>orm_ace_demo.hbp echo src/casts.prg
>>orm_ace_demo.hbp echo src/querybuilder.prg
>>orm_ace_demo.hbp echo src/schema.prg
>>orm_ace_demo.hbp echo src/model.prg
>>orm_ace_demo.hbp echo src/relations.prg
>>orm_ace_demo.hbp echo src/introspect.prg
>>orm_ace_demo.hbp echo src/dynmodel.prg
>>orm_ace_demo.hbp echo src/scaffold.prg
>>orm_ace_demo.hbp echo src/migration.prg
>>orm_ace_demo.hbp echo src/migrator.prg
>>orm_ace_demo.hbp echo src/navexec.prg
>>orm_ace_demo.hbp echo src/hbo_ace.prg
>>orm_ace_demo.hbp echo src/cursor.prg
>>orm_ace_demo.hbp echo src/browsesource.prg
>>orm_ace_demo.hbp echo src/browsebind.prg
>>orm_ace_demo.hbp echo -oorm_ace_demo
>>orm_ace_demo.hbp echo -w3
>>orm_ace_demo.hbp echo -Iinclude
>>orm_ace_demo.hbp echo -I%OPENADS_INC%
>>orm_ace_demo.hbp echo -lrddads
>>orm_ace_demo.hbp echo -L%OPENADS_LIB%
>>orm_ace_demo.hbp echo -lace64
rem CRT reconciliation (Harbour MSVC 64-bit + OpenADS CRT mix)
>>orm_ace_demo.hbp echo -ldflag=/NODEFAULTLIB:msvcrt
>>orm_ace_demo.hbp echo -ldflag=/NODEFAULTLIB:libucrt
>>orm_ace_demo.hbp echo -ldflag=/NODEFAULTLIB:libvcruntime.lib
>>orm_ace_demo.hbp echo -ldflag=/FORCE:MULTIPLE
>>orm_ace_demo.hbp echo -lucrt
>>orm_ace_demo.hbp echo -lvcruntime
>>orm_ace_demo.hbp echo -llegacy_stdio_definitions

if exist orm_ace_demo.exe del orm_ace_demo.exe
if exist orm_ace_demo.db  del orm_ace_demo.db

echo === BUILD ===
"%HB_BIN%" orm_ace_demo.hbp -static -cpu=x86_64 -gtstd
if not exist orm_ace_demo.exe ( echo BUILD_FAILED & exit /b 1 )

echo === RUN ===
copy /y "%OPENADS_LIB%\ace64.dll" . >nul
.\orm_ace_demo.exe
echo exit=%ERRORLEVEL%
endlocal
