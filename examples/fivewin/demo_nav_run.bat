@echo off
rem Console NAV smoke: TOpenAdsConnection + AdsOpenTable (sqlite/postgresql/mariadb/odbc).
rem Portable: paths via OPENADS_* env; no drive letter hardcoded.
rem Usage: demo_nav_run.bat [sqlite|odbc|pg|maria|all] [path-to-openace64-build-src]
setlocal EnableExtensions EnableDelayedExpansion
set "HERE=%~dp0"
set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"
set "OADLL=%~2"

for %%I in ("%HERE%..\..\..") do set "OPENADS_REPO=%%~fI"
if not defined OPENADS_WORKTREE_ROOT (
  for %%I in ("%HERE%..\..\..\..") do set "OPENADS_WORKTREE_ROOT=%%~fI"
)

if "%OADLL%"=="" call :resolve_dll "%MODE%"
if "%OADLL%"=="" (
  echo [ERRO] openace64.dll nao achada. Passe o 2o arg ou defina OPENADS_DLL_SRC.
  exit /b 1
)

if defined OPENADS_HB_BIN (
  set "HB=%OPENADS_HB_BIN%"
) else if defined HB_BIN (
  set "HB=%HB_BIN%"
) else (
  set "HB=hbmk2"
)
cd /d "%HERE%"

set "OPENADS_NAV_MODE=%MODE%"
if /I "%MODE%"=="pg" set "OPENADS_NAV_MODE=postgresql"

rem ODBC connstr only when explicitly configured (no default fixture path).
rem Credentials come from OPENADS_ODBC_UID / OPENADS_ODBC_PWD (no hardcoded secrets).
if not defined OPENADS_ODBC_UID set "OPENADS_ODBC_UID=SYSDBA"
if not defined OPENADS_TEST_ODBC_CONNSTR if defined OPENADS_ODBC_FIXTURE if defined OPENADS_ODBC_DRIVER (
  set "OPENADS_TEST_ODBC_CONNSTR=Driver={%OPENADS_ODBC_DRIVER%};Database=%OPENADS_ODBC_FIXTURE%;Uid=%OPENADS_ODBC_UID%;Pwd=%OPENADS_ODBC_PWD%;Charset=UTF8;"
)

> demo_nav_multidb.hbp echo demo_nav_multidb.prg
>>demo_nav_multidb.hbp echo ../../tools/fwh_patch/openads_ado_bridge.prg
>>demo_nav_multidb.hbp echo -w3
>>demo_nav_multidb.hbp echo -I../../include
>>demo_nav_multidb.hbp echo -DOPENADS_ADO_NO_FWH
rem NAV demo links ACE glue only (no rddads -- avoids ace64.dll vs openace64.dll split)
>>demo_nav_multidb.hbp echo -L%OADLL%
>>demo_nav_multidb.hbp echo -lopenace64
>>demo_nav_multidb.hbp echo -ldflag=/NODEFAULTLIB:msvcrt
>>demo_nav_multidb.hbp echo -ldflag=/NODEFAULTLIB:libucrt
>>demo_nav_multidb.hbp echo -ldflag=/NODEFAULTLIB:libvcruntime.lib
>>demo_nav_multidb.hbp echo -ldflag=/FORCE:MULTIPLE
>>demo_nav_multidb.hbp echo -lucrt
>>demo_nav_multidb.hbp echo -lvcruntime
>>demo_nav_multidb.hbp echo -llegacy_stdio_definitions

set "HBMK2="
set "HB_USER_OPTIONS="
if defined OPENADS_MSVC_SETUP call "%OPENADS_MSVC_SETUP%" >nul 2>&1
if exist demo_nav_multidb.exe del demo_nav_multidb.exe

echo === BUILD nav smoke (DLL: %OADLL%) ===
"%HB%" demo_nav_multidb.hbp -static -cpu=x86_64 -gtstd
if not exist demo_nav_multidb.exe (
  echo BUILD_FAILED
  exit /b 1
)

echo === PREP RUN ===
copy /y "%OADLL%\openace64.dll" . >nul
if defined OPENADS_MSVC_BIN (
  for %%D in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do (
    if not exist %%D copy /y "%OPENADS_MSVC_BIN%\%%D" . >nul 2>&1
  )
)
set "PATH=%OADLL%;%PATH%"
if defined FIREBIRD set "PATH=%FIREBIRD%;%PATH%"

echo === RUN mode=%OPENADS_NAV_MODE% ===
demo_nav_multidb.exe
set "RC=%ERRORLEVEL%"
echo exit=%RC%
endlocal & exit /b %RC%

:resolve_dll
set "MODEARG=%~1"
set "OADLL="
if /I "%MODEARG%"=="sqlite" goto :try_sqlpass
if /I "%MODEARG%"=="pg" goto :try_pg
if /I "%MODEARG%"=="postgresql" goto :try_pg
if /I "%MODEARG%"=="maria" goto :try_maria
if /I "%MODEARG%"=="mariadb" goto :try_maria
if /I "%MODEARG%"=="odbc" goto :try_odbc
goto :try_any

:try_sqlpass
if exist "%OPENADS_WORKTREE_ROOT%\OpenADS-sqlpass\build\ninja\src\openace64.dll" (
  set "OADLL=%OPENADS_WORKTREE_ROOT%\OpenADS-sqlpass\build\ninja\src"
  goto :eof
)
if exist "%OPENADS_REPO%\build\ninja\src\openace64.dll" (
  set "OADLL=%OPENADS_REPO%\build\ninja\src"
  goto :eof
)
goto :try_any

:try_pg
if exist "%OPENADS_WORKTREE_ROOT%\OpenADS-postgresql\build\pg-msvc\src\openace64.dll" (
  set "OADLL=%OPENADS_WORKTREE_ROOT%\OpenADS-postgresql\build\pg-msvc\src"
  goto :eof
)
if exist "%OPENADS_REPO%\build\pg-msvc\src\openace64.dll" (
  set "OADLL=%OPENADS_REPO%\build\pg-msvc\src"
  goto :eof
)
goto :try_any

:try_maria
if exist "%OPENADS_WORKTREE_ROOT%\OpenADS-mysql\build\maria-msvc\src\openace64.dll" (
  set "OADLL=%OPENADS_WORKTREE_ROOT%\OpenADS-mysql\build\maria-msvc\src"
  goto :eof
)
goto :try_any

:try_odbc
if exist "%OPENADS_WORKTREE_ROOT%\OpenADS-odbc\build\odbc-msvc\src\openace64.dll" (
  set "OADLL=%OPENADS_WORKTREE_ROOT%\OpenADS-odbc\build\odbc-msvc\src"
  goto :eof
)
if exist "%OPENADS_REPO%\build\odbc-msvc\src\openace64.dll" (
  set "OADLL=%OPENADS_REPO%\build\odbc-msvc\src"
  goto :eof
)
goto :try_any

:try_any
if defined OPENADS_DLL_SRC if exist "%OPENADS_DLL_SRC%\openace64.dll" (
  set "OADLL=%OPENADS_DLL_SRC%"
  goto :eof
)
if exist "%OPENADS_WORKTREE_ROOT%\OpenADS-odbc\build\odbc-msvc\src\openace64.dll" (
  set "OADLL=%OPENADS_WORKTREE_ROOT%\OpenADS-odbc\build\odbc-msvc\src"
)
if not defined OADLL if exist "%OPENADS_WORKTREE_ROOT%\OpenADS-sqlpass\build\ninja\src\openace64.dll" (
  set "OADLL=%OPENADS_WORKTREE_ROOT%\OpenADS-sqlpass\build\ninja\src"
)
if not defined OADLL if exist "%OPENADS_WORKTREE_ROOT%\OpenADS-postgresql\build\pg-msvc\src\openace64.dll" (
  set "OADLL=%OPENADS_WORKTREE_ROOT%\OpenADS-postgresql\build\pg-msvc\src"
)
goto :eof