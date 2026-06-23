@echo off
rem Build the exhaustion harness (exhaust.exe). Same env as build_run.bat:
rem   HB_BIN, OPENADS_DLLDIR, OPENADS_INCDIR, MSVC_SETUP
setlocal
set HERE=%~dp0
cd /d "%HERE%"
if not defined HB_BIN ( echo Set HB_BIN & exit /b 2 )
if not defined OPENADS_DLLDIR ( echo Set OPENADS_DLLDIR & exit /b 2 )
if not defined OPENADS_INCDIR ( echo Set OPENADS_INCDIR & exit /b 2 )

> exhaust.hbp echo tests/exhaust.prg
>>exhaust.hbp echo src/hbo_ace.prg
>>exhaust.hbp echo src/connection.prg
>>exhaust.hbp echo src/grammar.prg
>>exhaust.hbp echo src/querybuilder.prg
if exist src\schema.prg >>exhaust.hbp echo src/schema.prg
>>exhaust.hbp echo src/model.prg
>>exhaust.hbp echo -oexhaust
>>exhaust.hbp echo -w3
>>exhaust.hbp echo -Iinclude
>>exhaust.hbp echo -I%OPENADS_INCDIR%
>>exhaust.hbp echo -lrddads
>>exhaust.hbp echo -L%OPENADS_DLLDIR%
>>exhaust.hbp echo -lopenace64
>>exhaust.hbp echo -ldflag=/NODEFAULTLIB:msvcrt
>>exhaust.hbp echo -ldflag=/NODEFAULTLIB:libucrt
>>exhaust.hbp echo -ldflag=/NODEFAULTLIB:libvcruntime.lib
>>exhaust.hbp echo -ldflag=/FORCE:MULTIPLE
>>exhaust.hbp echo -lucrt
>>exhaust.hbp echo -lvcruntime
>>exhaust.hbp echo -llegacy_stdio_definitions

if defined MSVC_SETUP call "%MSVC_SETUP%" >nul 2>&1
set "HBMK2="
set "HB_USER_OPTIONS="
if exist exhaust.exe del exhaust.exe

echo === BUILD ===
"%HB_BIN%" exhaust.hbp -static -cpu=x86_64 -gtstd
if not exist exhaust.exe ( echo BUILD_FAILED & exit /b 1 )

echo === RUN ===
copy /y "%OPENADS_DLLDIR%\openace64.dll" . >nul
.\exhaust.exe
echo exit=%ERRORLEVEL%
endlocal
