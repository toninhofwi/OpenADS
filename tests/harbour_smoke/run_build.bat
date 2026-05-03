@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=c:\harbour\bin\win\msvc64;C:\OpenADS\build\default\src\Release;%PATH%
cd /d "%~dp0"

echo [smoke] Generating fixture data.dbf...
powershell -ExecutionPolicy Bypass -File make_data.ps1 || goto :err

echo [smoke] Generating fixture data.cdx...
"C:\OpenADS\build\default\tests\Release\make_cdx.exe" data.cdx || goto :err

echo [smoke] hbmk2 build...
hbmk2 -comp=msvc64 -gtcgi -I"c:\harbour\contrib\rddads" -lrddads -L"C:\OpenADS\build\default\src\Release" -lace64 -llegacy_stdio_definitions -loldnames smoke.prg
if errorlevel 1 goto :err

echo [smoke] Running smoke.exe...
.\smoke.exe
echo EXIT=%errorlevel%
goto :eof

:err
echo BUILD FAILED with errorlevel %errorlevel%
exit /b %errorlevel%
