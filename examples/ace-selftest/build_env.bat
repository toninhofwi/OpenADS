@echo off
REM build_env.bat - variaveis genericas (sem amarrar ao DEVAI)

set "OA_PROTO_ROOT=%~dp0"
if "%OA_PROTO_ROOT:~-1%"=="\" set "OA_PROTO_ROOT=%OA_PROTO_ROOT:~0,-1%"

if not defined HB_INSTALL set "HB_INSTALL=C:\harbour64"
if not defined MSVC_SETUP_X64 set "MSVC_SETUP_X64=C:\msvc\setup_x64.bat"
if not defined MSVC_UCRT_X64 set "MSVC_UCRT_X64=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"

if not defined OPENADS_LIB64 (
    if exist "%OA_PROTO_ROOT%\lib\openace64.lib" (
        set "OPENADS_LIB64=%OA_PROTO_ROOT%\lib\openace64.lib"
    )
)

if not defined OPENADS_DLL64 (
    if exist "%OA_PROTO_ROOT%\lib\openace64.dll" (
        set "OPENADS_DLL64=%OA_PROTO_ROOT%\lib\openace64.dll"
    )
)

if not defined OPENADS_INCLUDE (
    if exist "%OA_PROTO_ROOT%\..\OpenAds\include\openads\ace.h" (
        set "OPENADS_INCLUDE=%OA_PROTO_ROOT%\..\OpenAds\include"
    )
)

if exist "%~dp0build_env.local.bat" call "%~dp0build_env.local.bat"