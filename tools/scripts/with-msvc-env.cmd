@echo off
setlocal

set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "VSDEVCMD=%VSROOT%\Common7\Tools\VsDevCmd.bat"
set "VSINSTALLER=C:\Program Files (x86)\Microsoft Visual Studio\Installer"
set "CMAKEBIN=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJABIN=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

if not exist "%VSDEVCMD%" (
  echo VsDevCmd.bat not found: "%VSDEVCMD%" 1>&2
  exit /b 1
)

set "PATH=%VSINSTALLER%;%PATH%"
call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
set "PATH=%CMAKEBIN%;%NINJABIN%;%VSINSTALLER%;%PATH%"

if "%~1"=="" (
  echo MSVC x64 build environment loaded.
  echo Use: cmake, ninja, cl, msbuild
  cmd /k
) else (
  %*
)
