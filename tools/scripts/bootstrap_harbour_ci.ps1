# Bootstrap a minimal Harbour MSVC64 toolchain for CI.
# Installs into $InstallRoot (default: $env:RUNNER_TEMP\harbour-ci).
# Uses actions/cache keyed on this script's hash - first run builds from
# source (~45-90 min); subsequent runs reuse the cache.

param(
    [string]$InstallRoot = $(if ($env:HARBOUR_CI_ROOT) { $env:HARBOUR_CI_ROOT }
                             else { Join-Path $env:RUNNER_TEMP "harbour-ci" }),
    [string]$HarbourRepo = "https://github.com/harbour/core.git",
    [string]$HarbourRef  = "master"
)

$ErrorActionPreference = "Stop"
$env:GIT_TERMINAL_PROMPT = "0"

function Find-Hbmk2 {
    param([string]$Root)
    foreach ($rel in @(
        "bin\win\msvc64\hbmk2.exe",
        "bin\win\mingw64\hbmk2.exe",
        "bin\hbmk2.exe"
    )) {
        $p = Join-Path $Root $rel
        if (Test-Path $p) { return $p }
    }
    return $null
}

$hbmk2 = Find-Hbmk2 $InstallRoot
if ($hbmk2) {
    Write-Host "[harbour-ci] Reusing toolchain at $InstallRoot"
    Write-Host "[harbour-ci] hbmk2: $hbmk2"
    $env:HARBOUR_ROOT = $InstallRoot
    $env:PATH = "$(Split-Path $hbmk2);$env:PATH"
    exit 0
}

$src = Join-Path $env:RUNNER_TEMP "harbour-src"
if (-not (Test-Path (Join-Path $src "Makefile"))) {
    Write-Host "[harbour-ci] Cloning Harbour ($HarbourRef) ..."
    if (Test-Path $src) { Remove-Item -Recurse -Force $src }
    git clone --depth 1 --branch $HarbourRef $HarbourRepo $src
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[harbour-ci] git clone failed (exit $LASTEXITCODE)"
    }
    if (-not (Test-Path (Join-Path $src "Makefile"))) {
        Write-Error "[harbour-ci] Clone missing Makefile: $src"
    }
}

$winMake = Join-Path $src "win-make.exe"
if (-not (Test-Path $winMake)) {
    Write-Error "[harbour-ci] win-make.exe missing: $winMake"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Write-Error "[harbour-ci] No MSVC installation found" }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"

if (Test-Path $InstallRoot) { Remove-Item -Recurse -Force $InstallRoot }
New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
$prefix = ($InstallRoot -replace '\\', '/')

$buildLog = Join-Path $env:RUNNER_TEMP "harbour-build.log"
$buildCmd = Join-Path $env:RUNNER_TEMP "harbour-build.cmd"

# Batch file avoids quoting/env loss from inline cmd /c strings.
# Pass HB_* on the win-make command line (make vars) — more reliable than set on GHA.
$batch = @"
@echo off
setlocal EnableExtensions
cd /d "$src"
call "$vcvars" x64
if errorlevel 1 exit /b 1
set "ComSpec=C:\Windows\System32\cmd.exe"
set "SHELL=C:\Windows\System32\cmd.exe"
set SHLVL=
set MSYSTEM=
set MSYS2_PATH_TYPE=
"$winMake" install HB_PLATFORM=win HB_COMPILER=msvc64 HB_BUILD_3RDEXT=no HB_SHELL=nt HB_INSTALL_PREFIX=$prefix
exit /b %ERRORLEVEL%
"@
Set-Content -Path $buildCmd -Value $batch -Encoding ASCII

Write-Host "[harbour-ci] Building Harbour (win-make install -> $InstallRoot)"
Write-Host "[harbour-ci] Log: $buildLog"
Write-Host "[harbour-ci] Cold build may take 45-90 minutes."

cmd /c "`"$buildCmd`" > `"$buildLog`" 2>&1"
$buildExit = $LASTEXITCODE
Write-Host "[harbour-ci] win-make exit code: $buildExit"
if ($buildExit -ne 0) {
    Write-Host "[harbour-ci] Last 80 lines of build log:"
    Get-Content -Path $buildLog -Tail 80 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
    Write-Error "[harbour-ci] win-make install failed (exit $buildExit)"
}

$hbmk2 = Find-Hbmk2 $InstallRoot
if (-not $hbmk2) {
    Write-Error "[harbour-ci] hbmk2 not found under $InstallRoot after install"
}

$rddads = Join-Path $InstallRoot "contrib\rddads\rddads.ch"
if (-not (Test-Path $rddads)) {
    Write-Warning "[harbour-ci] contrib\rddads\rddads.ch missing"
}

$env:HARBOUR_ROOT = $InstallRoot
$env:PATH = "$(Split-Path $hbmk2);$env:PATH"
Write-Host "[harbour-ci] Ready: $hbmk2"