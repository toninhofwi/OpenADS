@echo off
setlocal
cd /d "%~dp0"

echo Setting up VS2022 x64 build environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

echo Building php_openads extension (ZTS x64)...
nmake /f Makefile.win %*

endlocal
exit /b %ERRORLEVEL%
