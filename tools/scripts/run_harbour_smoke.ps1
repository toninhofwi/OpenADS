# Build and run tests/smoke/harbour/smoke.prg against OpenADS ace64.
#
# Environment:
#   OPENADS_BUILD  - CMake build dir (default: build/msvc-x64)
#   HARBOUR_ROOT   - Harbour install root (default: C:\harbour or harbour-ci)
#   OPENADS_SKIP_HARBOUR_SMOKE=1 - exit 0 without running (local opt-out)

param(
    [string]$OpenAdsBuild = $(if ($env:OPENADS_BUILD) { $env:OPENADS_BUILD }
                              else { Join-Path $PSScriptRoot "..\..\build\default" }),
    [string]$HarbourRoot  = $env:HARBOUR_ROOT
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$smokeDir = Join-Path $repo "tests\smoke\harbour"

function Find-Hbmk2 {
    param([string]$Root)
    if (-not $Root) { return $null }
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

if ($env:OPENADS_SKIP_HARBOUR_SMOKE -eq "1") {
    Write-Host "[harbour-smoke] Skipped (OPENADS_SKIP_HARBOUR_SMOKE=1)"
    exit 0
}

$hbmk2 = $null
if ($HarbourRoot) {
    $hbmk2 = Find-Hbmk2 $HarbourRoot
}
if (-not $hbmk2) {
    foreach ($c in @("C:\harbour", "$env:RUNNER_TEMP\harbour-ci", "$env:HARBOUR_CI_ROOT")) {
        if (-not $c) { continue }
        $hbmk2 = Find-Hbmk2 $c
        if ($hbmk2) {
            $HarbourRoot = $c
            break
        }
    }
}
if (-not $hbmk2) {
    Write-Error "[harbour-smoke] Harbour not found. Set HARBOUR_ROOT or run bootstrap_harbour_ci.ps1"
}

$hbBin = Split-Path $hbmk2 -Parent
Write-Host "[harbour-smoke] hbmk2: $hbmk2"
Write-Host "[harbour-smoke] HB_INSTALL: $HarbourRoot"

$aceLib = Join-Path $OpenAdsBuild "src\Release"
$makeCdx = Join-Path $OpenAdsBuild "tests\Release\make_cdx.exe"
if (-not (Test-Path $makeCdx)) {
    Write-Error "[harbour-smoke] make_cdx.exe missing - build OpenADS first: $makeCdx"
}

$env:HB_INSTALL = $HarbourRoot
$env:OPENADS_LIB = $aceLib
$env:PATH = "$hbBin;$aceLib;$env:PATH"

Push-Location $smokeDir
try {
    Write-Host "[harbour-smoke] Generating data.dbf ..."
    powershell -ExecutionPolicy Bypass -File (Join-Path $smokeDir "make_data.ps1")

    Write-Host "[harbour-smoke] Generating data.cdx ..."
    & $makeCdx data.cdx
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[harbour-smoke] hbmk2 build ..."
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $hbmk2 -comp=msvc64 smoke.hbp 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        $ErrorActionPreference = $prevEap
    }

    Write-Host "[harbour-smoke] Running smoke.exe ..."
    & .\smoke.exe
    $code = $LASTEXITCODE
    Write-Host "[harbour-smoke] EXIT=$code"
    exit $code
}
finally {
    Pop-Location
}