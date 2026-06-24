@echo off
REM Copie para build_env.local.bat e ajuste os caminhos do SEU ambiente.
REM Nao commitar build_env.local.bat (contem paths locais).

set "HB_INSTALL=C:\harbour64"
set "MSVC_SETUP_X64=C:\msvc\setup_x64.bat"
set "MSVC_UCRT_X64=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"
set "OPENADS_INCLUDE=C:\OpenADS\include"
set "OPENADS_LIB64=C:\OpenADS\build\src\Release\openace64.lib"
set "OPENADS_DLL64=C:\OpenADS\build\src\Release\openace64.dll"

REM OpenADS wire port (openads_serverd default = 6262; change if that port is taken)
set "OPENADS_CONNECT_URI=tcp://127.0.0.1:6262/"