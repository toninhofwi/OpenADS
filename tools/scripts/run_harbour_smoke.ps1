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

function Import-VcVars {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { return $false }
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path $vcvars)) { return $false }
    cmd /c "`"$vcvars`" x64 >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2]
        }
    }
    return $true
}

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

$openAceDll = Join-Path $aceLib "openace64.dll"
if (-not (Test-Path $openAceDll)) {
    Write-Error "[harbour-smoke] openace64.dll missing - build openads_ace first: $openAceDll"
}

$aceDll = Join-Path $smokeDir "ace64.dll"
if (-not (Test-Path $aceDll)) {
    Copy-Item $openAceDll $aceDll -Force
    Write-Host "[harbour-smoke] Staged ace64.dll in smoke dir"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$aceLibImport = Join-Path $smokeDir "ace64.lib"
$builtAceLib = Join-Path $aceLib "openace64.lib"
$bundledAceLib = Join-Path $repoRoot "dist\import-libs\x64\msvc\ace64.lib"
$aceLibSource = if (Test-Path $builtAceLib) { $builtAceLib } elseif (Test-Path $bundledAceLib) { $bundledAceLib } else { $null }
if (-not $aceLibSource) {
    Write-Error "[harbour-smoke] Missing ace import lib (expected openace64.lib or dist/import-libs)"
}
if (-not (Test-Path $aceLibImport) -or ((Get-Item $aceLibSource).LastWriteTimeUtc -gt (Get-Item $aceLibImport).LastWriteTimeUtc)) {
    Copy-Item $aceLibSource $aceLibImport -Force
    Write-Host "[harbour-smoke] Staged ace64.lib from $aceLibSource"
}

$rddadsHdr = Join-Path $HarbourRoot "contrib\rddads\rddads.h"
if (-not (Test-Path $rddadsHdr)) {
    Write-Error "[harbour-smoke] contrib\rddads missing under $HarbourRoot - run bootstrap_harbour_ci.ps1"
}

$env:HB_INSTALL = $HarbourRoot
$env:OPENADS_LIB = if (Test-Path $aceLibImport) { $smokeDir } else { $aceLib }
$env:PATH = "$hbBin;$smokeDir;$aceLib;$env:PATH"

Push-Location $smokeDir
try {
    Write-Host "[harbour-smoke] Generating data.dbf ..."
    powershell -ExecutionPolicy Bypass -File (Join-Path $smokeDir "make_data.ps1")

    Write-Host "[harbour-smoke] Generating data.cdx ..."
    & $makeCdx data.cdx
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "[harbour-smoke] hbmk2 build ..."
    if (-not (Import-VcVars)) {
        Write-Error "[harbour-smoke] MSVC environment not found (vcvarsall.bat)"
    }
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $rddadsInc = Join-Path $HarbourRoot "contrib\rddads"
        & $hbmk2 -comp=msvc64 "-incpath=$rddadsInc" smoke.hbp 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        $ErrorActionPreference = $prevEap
    }

    Write-Host "[harbour-smoke] Running smoke.exe ..."
    $smokeExe = Join-Path $smokeDir "smoke.exe"
    if (-not (Test-Path $smokeExe)) {
        Write-Error "[harbour-smoke] smoke.exe missing after hbmk2 build: $smokeExe"
    }

    $smokeOut = Join-Path $smokeDir "smoke.out.txt"
    $smokeErr = Join-Path $smokeDir "smoke.err.txt"
    $proc = Start-Process -FilePath $smokeExe -WorkingDirectory $smokeDir `
        -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr `
        -NoNewWindow -Wait -PassThru
    $code = $proc.ExitCode
    if (Test-Path $smokeOut) {
        Get-Content $smokeOut | ForEach-Object { Write-Host $_ }
    }
    if (Test-Path $smokeErr) {
        $errText = Get-Content $smokeErr -Raw
        if ($errText) {
            Write-Host "[harbour-smoke] stderr:"
            Write-Host $errText
        }
    }
    Write-Host "[harbour-smoke] CDX smoke EXIT=$code"
    if ($code -ne 0) { exit $code }

    Write-Host "[harbour-smoke] hbmk2 build smoke_sqlite ..."
    $ErrorActionPreference = "Continue"
    try {
        $rddadsInc = Join-Path $HarbourRoot "contrib\rddads"
        & $hbmk2 -comp=msvc64 "-incpath=$rddadsInc" smoke_sqlite.hbp 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        $ErrorActionPreference = $prevEap
    }

    Write-Host "[harbour-smoke] Running smoke_sqlite.exe ..."
    $sqliteExe = Join-Path $smokeDir "smoke_sqlite.exe"
    if (-not (Test-Path $sqliteExe)) {
        Write-Error "[harbour-smoke] smoke_sqlite.exe missing after hbmk2 build: $sqliteExe"
    }

    $sqliteOut = Join-Path $smokeDir "smoke_sqlite.out.txt"
    $sqliteErr = Join-Path $smokeDir "smoke_sqlite.err.txt"
    $proc2 = Start-Process -FilePath $sqliteExe -WorkingDirectory $smokeDir `
        -RedirectStandardOutput $sqliteOut -RedirectStandardError $sqliteErr `
        -NoNewWindow -Wait -PassThru
    $code2 = $proc2.ExitCode
    if (Test-Path $sqliteOut) {
        Get-Content $sqliteOut | ForEach-Object { Write-Host $_ }
    }
    if (Test-Path $sqliteErr) {
        $errText2 = Get-Content $sqliteErr -Raw
        if ($errText2) {
            Write-Host "[harbour-smoke] smoke_sqlite stderr:"
            Write-Host $errText2
        }
    }
    Write-Host "[harbour-smoke] SQLite URI smoke EXIT=$code2"
    exit $code2
}
finally {
    Pop-Location
}