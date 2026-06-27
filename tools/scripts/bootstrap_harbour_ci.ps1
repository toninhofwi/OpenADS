# Bootstrap a minimal Harbour MSVC64 toolchain for CI.
# Installs into $InstallRoot (default: $env:RUNNER_TEMP\harbour-ci).
# Uses actions/cache keyed on this script's hash - first run builds from
# source (~45-90 min); subsequent runs reuse the cache.
#
# Harbour does not ship a root CMakeLists.txt; it builds with win-make + MSVC.

param(
    [string]$InstallRoot = $(if ($env:HARBOUR_CI_ROOT) { $env:HARBOUR_CI_ROOT }
                             else { Join-Path $env:RUNNER_TEMP "harbour-ci" }),
    [string]$HarbourRepo = "https://github.com/harbour/core.git",
    [string]$HarbourRef  = "master"
)

$ErrorActionPreference = "Stop"
$env:GIT_TERMINAL_PROMPT = "0"

$hbmk2 = Join-Path $InstallRoot "bin\win\msvc64\hbmk2.exe"
if (Test-Path $hbmk2) {
    Write-Host "[harbour-ci] Reusing cached toolchain at $InstallRoot"
    $env:HARBOUR_ROOT = $InstallRoot
    $hbBin = Join-Path $InstallRoot 'bin\win\msvc64'
    $env:PATH = "$hbBin;$env:PATH"
    exit 0
}

$src = Join-Path $env:RUNNER_TEMP "harbour-src"
$makefile = Join-Path $src "Makefile"
if (-not (Test-Path $makefile)) {
    Write-Host "[harbour-ci] Cloning Harbour ($HarbourRef) from $HarbourRepo ..."
    if (Test-Path $src) { Remove-Item -Recurse -Force $src }
    
    git clone --depth 1 --branch $HarbourRef $HarbourRepo $src
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[harbour-ci] git clone failed with exit code $LASTEXITCODE"
    }
    
    # Additional diagnostics if source directory is missing
    if (-not (Test-Path $src)) {
        Write-Error "[harbour-ci] Source directory not created after clone: $src"
    }
    
    # List directory contents for debugging
    if (Test-Path $src) {
        $items = Get-ChildItem $src -ErrorAction SilentlyContinue | Measure-Object
        Write-Host "[harbour-ci] Source directory contains $($items.Count) items"
    }
    
    if (-not (Test-Path $makefile)) {
        Write-Error "[harbour-ci] Harbour source tree missing after clone (no Makefile): $src"
    }
}

# Locate MSVC vcvarsall.bat (VS 2017+ / Build Tools).
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "[harbour-ci] vswhere.exe not found - MSVC toolchain required"
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    Write-Error "[harbour-ci] No MSVC installation found via vswhere"
}
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvars)) {
    Write-Error "[harbour-ci] vcvarsall.bat not found: $vcvars"
}

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
$prefix = $InstallRoot -replace '\\', '/'

Write-Host "[harbour-ci] Building Harbour with win-make (install -> $InstallRoot) ..."
Write-Host "[harbour-ci] This may take 45-90 minutes on a cold cache."

$buildCmd = @"
call "$vcvars" x64 && win-make install HB_INSTALL_PREFIX=$prefix
"@

Push-Location $src
try {
    cmd /c $buildCmd
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[harbour-ci] win-make install failed (exit $LASTEXITCODE)"
    }
} finally {
    Pop-Location
}

if (-not (Test-Path $hbmk2)) {
    Write-Error "[harbour-ci] hbmk2 not found after install: $hbmk2"
}

$rddads = Join-Path $InstallRoot "contrib\rddads\rddads.ch"
if (-not (Test-Path $rddads)) {
    Write-Warning "[harbour-ci] contrib\rddads not found at $rddads (smoke may still work via -inc)"
}

$env:HARBOUR_ROOT = $InstallRoot
$hbBin = Join-Path $InstallRoot 'bin\win\msvc64'
$env:PATH = "$hbBin;$env:PATH"
Write-Host "[harbour-ci] Ready: $hbmk2"
