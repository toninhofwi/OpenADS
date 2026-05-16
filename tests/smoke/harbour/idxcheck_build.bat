@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set ACE=C:\OpenADS\build\release-x64\src\Release
set PATH=c:\harbour\bin\win\msvc64;%ACE%;%PATH%
cd /d "%~dp0"
echo [idx] hbmk2 build...
hbmk2 -comp=msvc64 -gtcgi -I"c:\harbour\contrib\rddads" -lrddads -L"%ACE%" -lace64 -llegacy_stdio_definitions -loldnames idxprobe.prg
if errorlevel 1 goto :err
echo.
echo [idx] === DBFCDX baseline ===
.\idxprobe.exe
echo.
echo [idx] === ADSCDX -^> OpenADS ===
.\idxprobe.exe /ads
goto :eof
:err
echo BUILD FAILED %errorlevel%
exit /b %errorlevel%
