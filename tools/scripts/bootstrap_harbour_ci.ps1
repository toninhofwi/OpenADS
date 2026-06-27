# Bootstrap a minimal Harbour MSVC64 toolchain for CI.
# Installs into $InstallRoot (default: $env:RUNNER_TEMP\harbour-ci).
# Uses actions/cache keyed on this script's hash - first run builds from
# source (~45-90 min); subsequent runs reuse the cache.

param(
    [string]$InstallRoot = $(if ($env:HARBOUR_CI_ROOT) { $env:HARBOUR_CI_ROOT }
                             else { Join-Path $env:RUNNER_TEMP "harbour-ci" }),
    [string]$HarbourRepo = "https://github.com/harbour/core.git",
    [string]$HarbourRef  = "master",
    [string]$OpenAdsRoot  = $(if ($env:OPENADS_ROOT) { $env:OPENADS_ROOT }
                              else { (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path })
)

$ErrorActionPreference = "Stop"
$env:GIT_TERMINAL_PROMPT = "0"

function Get-VcVarsAll {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { return $null }
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path $vcvars)) { return $null }
    return $vcvars
}

function Import-VcVars {
    param([string]$VcVars = $(Get-VcVarsAll))
    if (-not $VcVars) { return $false }
    cmd /c "`"$VcVars`" x64 >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2]
        }
    }
    return $true
}

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

function Test-RddadsInstalled {
    param([string]$Root)
    $hdr = Join-Path $Root "contrib\rddads\rddads.h"
    $lib = Join-Path $Root "lib\win\msvc64\rddads.lib"
    return (Test-Path $hdr) -and (Test-Path $lib)
}

function Ensure-HarbourSrc {
    param(
        [string]$Src,
        [string]$HarbourRepo,
        [string]$HarbourRef
    )
    if (-not (Test-Path (Join-Path $Src "Makefile"))) {
        Write-Host "[harbour-ci] Cloning Harbour ($HarbourRef) ..."
        if (Test-Path $Src) { Remove-Item -Recurse -Force $Src }
        git clone --depth 1 --branch $HarbourRef $HarbourRepo $Src
        if ($LASTEXITCODE -ne 0) {
            Write-Error "[harbour-ci] git clone failed (exit $LASTEXITCODE)"
        }
        if (-not (Test-Path (Join-Path $Src "Makefile"))) {
            Write-Error "[harbour-ci] Clone missing Makefile: $Src"
        }
    }
    return $Src
}

function Install-RddadsContrib {
    param(
        [string]$HarbourSrc,
        [string]$InstallRoot,
        [string]$Hbmk2,
        [string]$OpenAdsRoot
    )

    $patch = Join-Path $OpenAdsRoot "tools\harbour_patch\rddads-compat.patch"
    if (-not (Test-Path $patch)) {
        Write-Error "[harbour-ci] Missing rddads patch: $patch"
    }

    Push-Location $HarbourSrc
    try {
        git apply --check $patch 2>$null
        if ($LASTEXITCODE -eq 0) {
            git apply $patch
            if ($LASTEXITCODE -ne 0) {
                Write-Error "[harbour-ci] git apply rddads-compat.patch failed"
            }
            Write-Host "[harbour-ci] Applied rddads-compat.patch"
        } else {
            Write-Host "[harbour-ci] rddads-compat.patch already applied (or upstream includes it)"
        }
    } finally {
        Pop-Location
    }

    $adsSdk = Join-Path $env:RUNNER_TEMP "openads-ads-sdk"
    if (Test-Path $adsSdk) { Remove-Item -Recurse -Force $adsSdk }
    New-Item -ItemType Directory -Force -Path $adsSdk | Out-Null

    $aceHdr = Join-Path $OpenAdsRoot "include\openads\ace.h"
    if (-not (Test-Path $aceHdr)) {
        Write-Error "[harbour-ci] Missing OpenADS ace.h: $aceHdr"
    }
    Copy-Item $aceHdr (Join-Path $adsSdk "ace.h")

    $aceLib = Join-Path $OpenAdsRoot "dist\import-libs\x64\msvc\ace64.lib"
    if (-not (Test-Path $aceLib)) {
        Write-Error "[harbour-ci] Missing OpenADS ace64 import lib: $aceLib"
    }
    Copy-Item $aceLib (Join-Path $adsSdk "ace64.lib")

    $buildDir = Join-Path $env:RUNNER_TEMP "rddads-build"
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
    Copy-Item -Recurse (Join-Path $HarbourSrc "contrib\rddads") $buildDir

    $env:HB_INSTALL = $InstallRoot
    $env:HB_WITH_ADS = $adsSdk
    $env:PATH = "$(Split-Path $Hbmk2);$env:PATH"

    if (-not (Import-VcVars)) {
        Write-Error "[harbour-ci] MSVC environment not found (vcvarsall.bat)"
    }

    Write-Host "[harbour-ci] Building contrib/rddads (HB_WITH_ADS=$adsSdk) ..."
    Push-Location $buildDir
    try {
        & $Hbmk2 -comp=msvc64 rddads.hbp 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            Write-Error "[harbour-ci] hbmk2 rddads.hbp failed (exit $LASTEXITCODE)"
        }
    } finally {
        Pop-Location
    }

    $builtLib = Join-Path $buildDir "rddads.lib"
    if (-not (Test-Path $builtLib)) {
        Write-Error "[harbour-ci] rddads.lib missing after build: $builtLib"
    }

    $contribDest = Join-Path $InstallRoot "contrib\rddads"
    $libDest = Join-Path $InstallRoot "lib\win\msvc64"
    New-Item -ItemType Directory -Force -Path $contribDest, $libDest | Out-Null

    foreach ($name in @("rddads.h", "ads.ch", "rddads.hbx")) {
        $srcFile = Join-Path $buildDir $name
        if (-not (Test-Path $srcFile)) {
            Write-Error "[harbour-ci] Missing rddads header: $srcFile"
        }
        Copy-Item $srcFile (Join-Path $contribDest $name) -Force
    }
    Copy-Item $builtLib (Join-Path $libDest "rddads.lib") -Force
    Copy-Item $builtLib (Join-Path $contribDest "rddads.lib") -Force

    Write-Host "[harbour-ci] Installed rddads to $contribDest and $libDest"
}

$src = Join-Path $env:RUNNER_TEMP "harbour-src"
$hbmk2 = Find-Hbmk2 $InstallRoot
if ($hbmk2 -and (Test-RddadsInstalled $InstallRoot)) {
    Write-Host "[harbour-ci] Reusing toolchain at $InstallRoot"
    Write-Host "[harbour-ci] hbmk2: $hbmk2"
    $env:HARBOUR_ROOT = $InstallRoot
    $env:PATH = "$(Split-Path $hbmk2);$env:PATH"
    exit 0
}

if ($hbmk2) {
    Write-Host "[harbour-ci] Harbour core cached; building missing contrib/rddads ..."
    $src = Ensure-HarbourSrc $src $HarbourRepo $HarbourRef
    Install-RddadsContrib -HarbourSrc $src -InstallRoot $InstallRoot `
        -Hbmk2 $hbmk2 -OpenAdsRoot $OpenAdsRoot
    $env:HARBOUR_ROOT = $InstallRoot
    $env:PATH = "$(Split-Path $hbmk2);$env:PATH"
    exit 0
}

$src = Ensure-HarbourSrc $src $HarbourRepo $HarbourRef

$winMake = Join-Path $src "win-make.exe"
if (-not (Test-Path $winMake)) {
    Write-Error "[harbour-ci] win-make.exe missing: $winMake"
}

$vcvars = Get-VcVarsAll
if (-not $vcvars) { Write-Error "[harbour-ci] No MSVC installation found" }

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

Install-RddadsContrib -HarbourSrc $src -InstallRoot $InstallRoot `
    -Hbmk2 $hbmk2 -OpenAdsRoot $OpenAdsRoot

if (-not (Test-RddadsInstalled $InstallRoot)) {
    Write-Error "[harbour-ci] contrib\rddads install verification failed"
}

$env:HARBOUR_ROOT = $InstallRoot
$env:PATH = "$(Split-Path $hbmk2);$env:PATH"
Write-Host "[harbour-ci] Ready: $hbmk2"