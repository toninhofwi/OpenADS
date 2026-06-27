# Bootstrap a minimal Harbour MSVC64 toolchain for CI.
# Installs into $InstallRoot (default: $env:RUNNER_TEMP\harbour-ci).
# Uses actions/cache keyed on this script's hash - first run builds from
# source (~45-90 min); subsequent runs reuse the cache.
#
# Harbour builds with win-make (not CMake).  CI gotchas:
#   - Do NOT set HB_COMPILER=msvc on x64 (breaks includes -> "No rule to make target '.mk'").
#     Let vcvars x64 + ml64.exe auto-detect msvc64.
#   - Force SHELL=cmd.exe so Git's usr/bin/sh.exe is not picked up.
#   - HB_BUILD_3RDEXT=no skips bundled png/zlib/etc. (not needed for rddads smoke).

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
    $candidates = @(
        (Join-Path $Root "bin\win\msvc64\hbmk2.exe"),
        (Join-Path $Root "bin\win\mingw64\hbmk2.exe"),
        (Join-Path $Root "bin\hbmk2.exe")
    )
    foreach ($p in $candidates) {
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
$makefile = Join-Path $src "Makefile"
if (-not (Test-Path $makefile)) {
    Write-Host "[harbour-ci] Cloning Harbour ($HarbourRef) ..."
    if (Test-Path $src) { Remove-Item -Recurse -Force $src }
    git clone --depth 1 --branch $HarbourRef $HarbourRepo $src
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[harbour-ci] git clone failed (exit $LASTEXITCODE)"
    }
    if (-not (Test-Path $makefile)) {
        Write-Error "[harbour-ci] Clone missing Makefile: $src"
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "[harbour-ci] vswhere.exe not found"
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    Write-Error "[harbour-ci] No MSVC installation found"
}
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvars)) {
    Write-Error "[harbour-ci] vcvarsall.bat not found: $vcvars"
}

# Drop a poisoned partial cache so the next save is clean.
if (Test-Path $InstallRoot) {
    Remove-Item -Recurse -Force $InstallRoot
}
New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
$prefix = $InstallRoot -replace '\\', '/'

$buildLog = Join-Path $env:RUNNER_TEMP "harbour-build.log"
Write-Host "[harbour-ci] Building Harbour (win-make install -> $InstallRoot)"
Write-Host "[harbour-ci] Log: $buildLog"
Write-Host "[harbour-ci] Cold build may take 45-90 minutes."

# Use Harbour's bundled .\win-make.exe (not a random make from PATH/Git).
# Force msvc64/x64 — auto-detect often picks mingw64 on GHA runners.
$winMake = Join-Path $src "win-make.exe"
if (-not (Test-Path $winMake)) {
    Write-Error "[harbour-ci] win-make.exe missing in clone: $winMake"
}

$buildCmd = @"
cd /d "$src" && call "$vcvars" x64 && set "SHELL=%ComSpec%" && set HB_PLATFORM=win && set HB_COMPILER=msvc64 && set HB_BUILD_3RDEXT=no && set HB_SHELL=nt && "$winMake" install HB_INSTALL_PREFIX=$prefix
"@

cmd /c "$buildCmd > `"$buildLog`" 2>&1"
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
    Write-Warning "[harbour-ci] contrib\rddads\rddads.ch missing (smoke uses -inc={HB_INSTALL}\contrib\rddads)"
}

$env:HARBOUR_ROOT = $InstallRoot
$env:PATH = "$(Split-Path $hbmk2);$env:PATH"
Write-Host "[harbour-ci] Ready: $hbmk2"