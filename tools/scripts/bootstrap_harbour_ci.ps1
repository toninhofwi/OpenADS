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

# Try multiple possible locations for hbmk2
$possibleHbmk2Paths = @(
    (Join-Path $InstallRoot "bin\win\msvc64\hbmk2.exe"),
    (Join-Path $InstallRoot "bin\win\mingw64\hbmk2.exe"),
    (Join-Path $InstallRoot "bin\hbmk2.exe")
)

$hbmk2 = $null
foreach ($path in $possibleHbmk2Paths) {
    if (Test-Path $path) {
        $hbmk2 = $path
        break
    }
}

if ($hbmk2) {
    Write-Host "[harbour-ci] Reusing cached toolchain at $InstallRoot"
    Write-Host "[harbour-ci] Found hbmk2 at: $hbmk2"
    $env:HARBOUR_ROOT = $InstallRoot
    $hbBin = Split-Path $hbmk2
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

# Create a log file for the build output
$buildLog = Join-Path $env:RUNNER_TEMP "harbour-build.log"
Write-Host "[harbour-ci] Building Harbour with win-make (install -> $InstallRoot) ..."
Write-Host "[harbour-ci] Build log: $buildLog"
Write-Host "[harbour-ci] This may take 45-90 minutes on a cold cache."
Write-Host "[harbour-ci] VCVARS: $vcvars"

# Build command that sets MSVC environment and compiles with MSVC
$buildCmd = @"
cd /d "$src" && call "$vcvars" x64 && set HB_COMPILER=msvc && win-make install HB_INSTALL_PREFIX=$prefix 2>&1
"@

try {
    # Run build and capture output to file AND console
    Write-Host "[harbour-ci] Executing build command..."
    cmd /c $buildCmd | Tee-Object -FilePath $buildLog
    $buildExitCode = $LASTEXITCODE
    
    Write-Host "[harbour-ci] Build exit code: $buildExitCode"
    
    if ($buildExitCode -ne 0) {
        Write-Host "[harbour-ci] WARNING: Build completed with exit code $buildExitCode"
        Write-Host "[harbour-ci] Last 150 lines of build output:"
        Write-Host "=========================================="
        Get-Content -Path $buildLog -Tail 150
        Write-Host "=========================================="
        Write-Host "[harbour-ci] Continuing to check for installed binaries..."
    }
} catch {
    Write-Error "[harbour-ci] Build execution failed: $_"
}

# Check if install was successful - search for hbmk2 in multiple locations
Write-Host "[harbour-ci] Checking installation at $InstallRoot ..."

$hbmk2 = $null
foreach ($path in $possibleHbmk2Paths) {
    if (Test-Path $path) {
        $hbmk2 = $path
        Write-Host "[harbour-ci] Found hbmk2 at: $hbmk2"
        break
    }
}

if (Test-Path (Join-Path $InstallRoot "bin")) {
    Write-Host "[harbour-ci] Contents of bin directory structure:"
    Get-ChildItem -Path (Join-Path $InstallRoot "bin") -Recurse -ErrorAction SilentlyContinue | ForEach-Object { 
        Write-Host "  $($_.FullName)" 
    }
} else {
    Write-Host "[harbour-ci] WARNING: No bin directory found in $InstallRoot"
}

# Check lib directory as well
if (Test-Path (Join-Path $InstallRoot "lib")) {
    Write-Host "[harbour-ci] Contents of lib directory (first 20 items):"
    Get-ChildItem -Path (Join-Path $InstallRoot "lib") -Recurse -ErrorAction SilentlyContinue | Select-Object -First 20 | ForEach-Object { 
        Write-Host "  $($_.FullName)" 
    }
}

# Final check for hbmk2
if (-not $hbmk2) {
    Write-Error "[harbour-ci] hbmk2 not found after install!`n`nSearched in:`n  - $($possibleHbmk2Paths -join "`n  - ")`n`nInstallation may have failed. Check build log above for details."
}

Write-Host "[harbour-ci] SUCCESS: hbmk2 found at: $hbmk2"

$rddads = Join-Path $InstallRoot "contrib\rddads\rddads.ch"
if (-not (Test-Path $rddads)) {
    Write-Warning "[harbour-ci] contrib\rddads not found at $rddads (smoke may still work via -inc)"
}

$env:HARBOUR_ROOT = $InstallRoot
$hbBin = Split-Path $hbmk2
$env:PATH = "$hbBin;$env:PATH"
Write-Host "[harbour-ci] Ready: $hbmk2"
Write-Host "[harbour-ci] PATH updated with: $hbBin"
