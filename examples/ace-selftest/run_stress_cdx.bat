@echo off
setlocal
call "%~dp0build_env.bat"
cd /d "%OA_PROTO_ROOT%"

if not exist oa_stress_cdx.exe (
    call "%~dp0build_stress_cdx.bat"
    if errorlevel 1 exit /b 1
)

if defined OPENADS_DLL64 if exist "%OPENADS_DLL64%" (
    copy /y "%OPENADS_DLL64%" "%OA_PROTO_ROOT%\" >nul
)

REM Uso: run_stress_cdx.bat [pasta] [loops] [batch] [fresh^|reuse^|reindex]
REM Padrao: data_stress 100 50 reuse
oa_stress_cdx.exe %*
exit /b %ERRORLEVEL%