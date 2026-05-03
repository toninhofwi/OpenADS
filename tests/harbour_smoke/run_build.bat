@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=c:\harbour\bin\win\msvc64;C:\OpenADS\build\default\src\Release;%PATH%
cd /d "%~dp0"
hbmk2 -comp=msvc64 -I"c:\harbour\contrib\rddads" -lrddads -L"C:\OpenADS\build\default\src\Release" -lace64 smoke.prg
echo EXIT=%errorlevel%
