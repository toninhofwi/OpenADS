@echo off
setlocal
call "%~dp0build_env.bat"

if not exist "%HB_INSTALL%\bin\hbmk2.exe" (
    echo [ERRO] hbmk2 nao encontrado em HB_INSTALL=%HB_INSTALL%
    exit /b 1
)
if not exist "%OPENADS_LIB64%" (
    echo [ERRO] OPENADS_LIB64 nao definido
    exit /b 1
)
if not defined OPENADS_INCLUDE (
    echo [ERRO] OPENADS_INCLUDE nao definido
    exit /b 1
)

if exist "%MSVC_SETUP_X64%" call "%MSVC_SETUP_X64%" 2>nul

cd /d "%OA_PROTO_ROOT%"

if not exist "oa_ace.prg" exit /b 1
findstr /c:"OAA_CONNECT" oa_proto_common.prg >nul 2>&1 || exit /b 1

"%HB_INSTALL%\bin\hbmk2.exe" oa_stress_cdx.hbp -w3 -static -cpu=x86_64 -ooa_stress_cdx
if errorlevel 1 (
    echo [ERRO] compilacao oa_stress_cdx falhou
    exit /b 1
)

echo [OK] oa_stress_cdx.exe
endlocal