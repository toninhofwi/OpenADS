@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d F:\OpenADS\bindings\php_ext
nmake /f Makefile.win
exit /b %ERRORLEVEL%
