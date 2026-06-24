@echo off
REM check_ace.bat - valida fontes ACE antes de compilar (rode no CMD local)
setlocal
cd /d "%~dp0"

set "OK=1"

if not exist "oa_ace.prg" (
    echo [FALHA] oa_ace.prg ausente
    set "OK=0"
)

findstr /c:"OAA_CONNECT" oa_proto_common.prg >nul 2>&1
if errorlevel 1 (
    echo [FALHA] oa_proto_common.prg sem OAA_CONNECT ^(versao RDD^)
    set "OK=0"
) else (
    echo [OK] oa_proto_common.prg = ACE
)

findstr /c:"oa_ace.prg" oa_proto_adt.hbp >nul 2>&1
if errorlevel 1 (
    echo [FALHA] oa_proto_adt.hbp sem oa_ace.prg
    set "OK=0"
) else (
    echo [OK] oa_proto_adt.hbp referencia oa_ace.prg
)

if "%OK%"=="1" (
    echo.
    echo Fontes ACE validas. Pode rodar: build.bat
    exit /b 0
)

echo.
echo Corrija as fontes ou sincronize a pasta antes de compilar.
exit /b 1
endlocal