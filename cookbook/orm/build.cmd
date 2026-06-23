@echo off
rem ===================================================================
rem  Generic builder for the OpenADS cookbook -- ORM track.
rem  No drive letters: everything comes from arguments / environment.
rem
rem  Usage:
rem     build.cmd <example.prg> <openads-lib-dir>
rem
rem     <example.prg>      e.g. sqlite\crud_sqlite.prg
rem     <openads-lib-dir>  folder with the OpenADS DLL + import lib
rem
rem  Environment:
rem     OADS_ORM_SRC    folder with the companion ORM sources (its src\)
rem                     -- clone the ORM repo and point this at its src\.
rem     OPENADS_INCDIR  folder holding openads\ace.h (for the ORM C glue)
rem     OPENADS_ACELIB  import-lib base name (default openace64)
rem     HB_BIN          full path to hbmk2 (optional if on PATH)
rem     MSVC_SETUP      MSVC x64 env .bat (optional if already in env)
rem ===================================================================
setlocal EnableDelayedExpansion

if "%~2"=="" ( echo Usage: build.cmd ^<example.prg^> ^<openads-lib-dir^> & exit /b 2 )
set "EXAMPLE=%~1"
set "OPENADS_LIB=%~2"
if not defined OADS_ORM_SRC ( echo Set OADS_ORM_SRC to the companion ORM src folder & exit /b 2 )
if not defined OPENADS_INCDIR ( echo Set OPENADS_INCDIR to the folder holding openads\ace.h & exit /b 2 )
if not defined OPENADS_ACELIB set "OPENADS_ACELIB=openace64"
set "HBMK=hbmk2"
if defined HB_BIN set "HBMK=%HB_BIN%"

pushd "%~dp0"
for %%F in ("%EXAMPLE%") do set "EXEDIR=%%~dpF"
for %%F in ("%EXAMPLE%") do set "EXEBASE=%%~nF"
set "HBP=%EXEDIR%%EXEBASE%.hbp"

rem Generate the .hbp: the example + the ORM sources + the link line.
rem (the example entry is the bare basename -- hbmk2 resolves source
rem  paths relative to the .hbp file's own directory)
> "%HBP%"  echo %EXEBASE%.prg
>>"%HBP%" echo %OADS_ORM_SRC%\hbo_ace.prg
>>"%HBP%" echo %OADS_ORM_SRC%\connection.prg
>>"%HBP%" echo %OADS_ORM_SRC%\grammar.prg
>>"%HBP%" echo %OADS_ORM_SRC%\querybuilder.prg
>>"%HBP%" echo %OADS_ORM_SRC%\schema.prg
>>"%HBP%" echo %OADS_ORM_SRC%\model.prg
>>"%HBP%" echo -I%OADS_ORM_SRC%\..\include
>>"%HBP%" echo -I%OPENADS_INCDIR%
>>"%HBP%" echo -lrddads
>>"%HBP%" echo -L%OPENADS_LIB%
>>"%HBP%" echo -l%OPENADS_ACELIB%
rem CRT-compat (see ../docs/building-and-running.md):
>>"%HBP%" echo -ldflag=/NODEFAULTLIB:msvcrt
>>"%HBP%" echo -ldflag=/NODEFAULTLIB:libucrt
>>"%HBP%" echo -ldflag=/NODEFAULTLIB:libvcruntime.lib
>>"%HBP%" echo -ldflag=/FORCE:MULTIPLE
>>"%HBP%" echo -lucrt
>>"%HBP%" echo -lvcruntime
>>"%HBP%" echo -llegacy_stdio_definitions

if defined MSVC_SETUP call "%MSVC_SETUP%" >nul 2>&1
set "HBMK2="
set "HB_USER_OPTIONS="
set "PATH=%OPENADS_LIB%;%PATH%"

echo === Building %EXAMPLE% ===
"%HBMK%" "%HBP%" -o"%EXEDIR%%EXEBASE%" -comp=msvc64 -cpu=x86_64 -static -gtstd
if errorlevel 1 ( echo BUILD FAILED & popd & exit /b 1 )

copy /y "%OPENADS_LIB%\%OPENADS_ACELIB%.dll" "%EXEDIR%" >nul 2>&1
echo === Build OK : %EXEDIR%%EXEBASE%.exe ===
popd
endlocal
