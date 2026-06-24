@echo off
setlocal
call "%~dp0build_env.bat"
cd /d "%OA_PROTO_ROOT%"

if not exist oa_proto_adt.exe (
    call "%~dp0build_adt.bat"
    if errorlevel 1 exit /b 1
)

if defined OPENADS_DLL64 if exist "%OPENADS_DLL64%" (
    copy /y "%OPENADS_DLL64%" "%OA_PROTO_ROOT%\" >nul
)

REM O .prg gera o banco do zero (AdsCreateTable ADT+ADI+seed). Este bat NAO prepara dados.
REM Uso: run_adt.bat [pasta] [local^|remote] [uri]
REM Ex remoto: run_adt.bat data_adt remote tcp://127.0.0.1:6262/
oa_proto_adt.exe %*
exit /b %ERRORLEVEL%