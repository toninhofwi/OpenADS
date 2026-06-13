@echo off
rem build_msvc64.cmd — build xbrowse_ads.prg with FiveWin + Harbour +
rem MSVC 64-bit, linking Harbour's rddads contrib + OpenADS' ace64.lib
rem (so the produced exe drives Advantage tables through OpenADS' DLL).
rem
rem Adapted from FWH's samples\build_new.bat :HM64 path, with two extra
rem link entries: rddads.lib (Harbour's ADSCDX/ADSNTX RDD) and
rem ace64.lib (import lib for OpenADS' ace64.dll).
rem
rem Usage: build_msvc64.cmd [path-to-openads-ace64-dir]
rem   default: ..\..\build\default\src\Release
rem Run xbrowse_ads.exe with OpenADS' ace64.dll on PATH (the script
rem copies it next to the exe). xbrowse_ads.exe /auto = self-closing.

setlocal
if "%FWDIR%"=="" set "FWDIR=c:\fwteam"
if "%HBDIR%"=="" set "HBDIR=c:\harbour"
set "HDIRL=%HBDIR%\lib\win\msvc64"
set "OPENADS_DLL=%~1"
if "%OPENADS_DLL%"=="" set "OPENADS_DLL=%~dp0..\..\build\default\src\Release"
set "PRG=%~2"
if "%PRG%"=="" set "PRG=xbrowse_ads"

rem MSVC environment (prefer a complete VS 2022; fall back to VS 2026)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
) else (
  call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 >nul
)
where link.exe

echo [fwh] harbour %PRG%.prg ...
"%HBDIR%\bin\win\msvc64\harbour" %PRG% /n /i"%FWDIR%\include";"%HBDIR%\include" /w /p /d__64__ || goto :err

echo [fwh] cl %PRG%.c ...
cl -TC -W3 -O2 -c -I"%HBDIR%\include" -D_WIN64 -D__FLAT__ -I"%FWDIR%\include" %PRG%.c || goto :err

echo [fwh] link ...
rem Lib list mirrors FWH samples\build_new.bat :HM64, plus rddads.lib
rem (Harbour's ADS RDD) and OpenADS' ace64.lib.
> msvc.tmp echo %PRG%.obj
>> msvc.tmp echo "%FWDIR%\lib\FiveH64.lib" "%FWDIR%\lib\FiveHC64.lib" "%FWDIR%\lib\libmariadb64.lib"
>> msvc.tmp echo "%FWDIR%\lib\hbhpdf64.lib"
>> msvc.tmp echo "%FWDIR%\lib\libhpdf64.lib"
>> msvc.tmp echo "%HDIRL%\hbrtl.lib"
>> msvc.tmp echo "%HDIRL%\hbvm.lib"
>> msvc.tmp echo "%HDIRL%\gtgui.lib"
>> msvc.tmp echo "%HDIRL%\hblang.lib"
>> msvc.tmp echo "%HDIRL%\hbmacro.lib"
>> msvc.tmp echo "%HDIRL%\hbrdd.lib"
>> msvc.tmp echo "%HDIRL%\rddntx.lib"
>> msvc.tmp echo "%HDIRL%\rddcdx.lib"
>> msvc.tmp echo "%HDIRL%\rddfpt.lib"
>> msvc.tmp echo "%HDIRL%\hbsix.lib"
>> msvc.tmp echo "%HDIRL%\rddads.lib"
>> msvc.tmp echo "%OPENADS_DLL%\openace64.lib"
>> msvc.tmp echo "%HDIRL%\hbdebug.lib"
>> msvc.tmp echo "%HDIRL%\hbcommon.lib"
>> msvc.tmp echo "%HDIRL%\hbpp.lib"
>> msvc.tmp echo "%HDIRL%\hbcpage.lib"
>> msvc.tmp echo "%HDIRL%\hbwin.lib"
>> msvc.tmp echo "%HDIRL%\hbct.lib"
>> msvc.tmp echo "%HDIRL%\hbziparc.lib"
>> msvc.tmp echo "%HDIRL%\hbmzip.lib"
>> msvc.tmp echo "%HDIRL%\hbzlib.lib"
>> msvc.tmp echo "%HDIRL%\hbpcre.lib"
>> msvc.tmp echo "%HDIRL%\minizip.lib"
>> msvc.tmp echo "%HDIRL%\xhb.lib"
>> msvc.tmp echo "%HDIRL%\hbcplr.lib"
>> msvc.tmp echo "%HDIRL%\png.lib"
>> msvc.tmp echo "%HDIRL%\hbtip.lib"
>> msvc.tmp echo "%HDIRL%\hbzebra.lib"
>> msvc.tmp echo "%HDIRL%\hbcurl.lib"
>> msvc.tmp echo "%HDIRL%\libcurl.lib"
>> msvc.tmp echo kernel32.lib user32.lib gdi32.lib winspool.lib comctl32.lib comdlg32.lib
>> msvc.tmp echo advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib
>> msvc.tmp echo odbccp32.lib iphlpapi.lib mpr.lib version.lib wsock32.lib msimg32.lib
>> msvc.tmp echo oledlg.lib psapi.lib gdiplus.lib winmm.lib vfw32.lib ws2_32.lib
>> msvc.tmp echo strmiids.lib ucrt.lib UxTheme.lib
if exist %PRG%.res >> msvc.tmp echo %PRG%.res
del /q %PRG%.exe 2>nul
rem Drive the link through cl (resolves the MSVC toolchain reliably; a
rem bare `link` can pick up a same-named tool from PATH).
cl @msvc.tmp /Fe:%PRG%.exe /nologo /link /subsystem:windows /NODEFAULTLIB:libucrt /NODEFAULTLIB:msvcrt || goto :err
if not exist %PRG%.exe goto :err

echo [fwh] copying OpenADS openace64.dll next to the exe ...
copy /y "%OPENADS_DLL%\openace64.dll" . >nul 2>&1

echo [fwh] done: %PRG%.exe   (smoke run: %PRG%.exe /auto)
endlocal & exit /b 0

:err
echo [fwh] BUILD FAILED (errorlevel %errorlevel%)
endlocal & exit /b 1
