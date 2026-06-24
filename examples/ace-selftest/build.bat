@echo off
call "%~dp0check_ace.bat"
if errorlevel 1 exit /b 1
call "%~dp0build_dbf.bat"
if errorlevel 1 exit /b 1
call "%~dp0build_adt.bat"
exit /b %ERRORLEVEL%