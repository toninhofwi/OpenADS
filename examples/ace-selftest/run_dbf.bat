@echo off
setlocal
call "%~dp0build_env.bat"
cd /d "%OA_PROTO_ROOT%"

if not exist oa_proto_dbf.exe (
    call "%~dp0build_dbf.bat"
    if errorlevel 1 exit /b 1
)

if defined OPENADS_DLL64 if exist "%OPENADS_DLL64%" (
    copy /y "%OPENADS_DLL64%" "%OA_PROTO_ROOT%\" >nul
)

REM O .prg gera o banco do zero (AdsCreateTable+CDX+seed). Este bat NAO prepara dados.
REM Uso: run_dbf.bat [pasta] [local^|remote] [uri]
REM Ex remoto: run_dbf.bat data_dbf remote tcp://127.0.0.1:6262/
oa_proto_dbf.exe %*
exit /b %ERRORLEVEL%