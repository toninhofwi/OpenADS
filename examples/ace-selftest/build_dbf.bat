@echo off
setlocal
call "%~dp0build_env.bat"

if not exist "%HB_INSTALL%\bin\hbmk2.exe" (
    echo [ERRO] hbmk2 nao encontrado em HB_INSTALL=%HB_INSTALL%
    echo Copie build_env.local.example.bat para build_env.local.bat e ajuste os paths.
    exit /b 1
)
if not exist "%OPENADS_LIB64%" (
    echo [ERRO] openace64.lib nao encontrado. Defina OPENADS_LIB64 em build_env.local.bat
    exit /b 1
)
if not defined OPENADS_INCLUDE (
    echo [ERRO] OPENADS_INCLUDE nao definido. Defina em build_env.local.bat
    exit /b 1
)
if not exist "%OPENADS_INCLUDE%\openads\ace.h" (
    echo [ERRO] ace.h nao encontrado em OPENADS_INCLUDE=%OPENADS_INCLUDE%
    exit /b 1
)

if exist "%MSVC_SETUP_X64%" call "%MSVC_SETUP_X64%" 2>nul

cd /d "%OA_PROTO_ROOT%"

if not exist "oa_ace.prg" (
    echo [ERRO] oa_ace.prg ausente - projeto precisa da glue ACE
    exit /b 1
)

findstr /c:"oa_ace.prg" oa_proto_dbf.hbp >nul 2>&1
if errorlevel 1 (
    echo [ERRO] oa_proto_dbf.hbp sem oa_ace.prg - abortando build RDD antigo
    exit /b 1
)

findstr /c:"OAA_CONNECT" oa_proto_common.prg >nul 2>&1
if errorlevel 1 (
    echo [ERRO] oa_proto_common.prg e versao RDD - precisa OAA_CONNECT
    exit /b 1
)

"%HB_INSTALL%\bin\hbmk2.exe" oa_proto_dbf.hbp -w3 -static -cpu=x86_64 -ooa_proto_dbf
if errorlevel 1 (
    echo [ERRO] compilacao oa_proto_dbf falhou
    exit /b 1
)

echo [OK] oa_proto_dbf.exe

echo [SMOKE] ACE DBF...
rd /s /q data_dbf 2>nul
if defined OPENADS_DLL64 if exist "%OPENADS_DLL64%" (
    copy /y "%OPENADS_DLL64%" "%OA_PROTO_ROOT%\" >nul
)
oa_proto_dbf.exe data_dbf 2>&1 | findstr /c:"ACE API" >nul
if errorlevel 1 (
    echo [ERRO] exe compilado nao e versao ACE - verifique fontes em disco
    exit /b 1
)
echo [SMOKE] PASS

endlocal