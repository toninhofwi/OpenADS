# Shared helpers for ADS vs OpenADS engine bench (portable paths).
# All machine-specific locations come from environment variables; there are no
# hardcoded install paths. Set the OPENADS_* / HB_INSTALL / *_LIB vars below.

function Get-OpenAdsRepoRoot {
    if ($env:OPENADS_REPO_ROOT) {
        return (Resolve-Path $env:OPENADS_REPO_ROOT).Path
    }
    $here = Split-Path -Parent $PSScriptRoot
    return (Resolve-Path (Join-Path $here "..")).Path
}

function Get-HarbourInstall {
    if ($env:HB_INSTALL -and (Test-Path $env:HB_INSTALL)) {
        return (Resolve-Path $env:HB_INSTALL).Path
    }
    if ($env:OPENADS_HB_BIN) {
        $bin = Split-Path -Parent $env:OPENADS_HB_BIN
        $root = Split-Path -Parent $bin
        if (Test-Path (Join-Path $root "lib\ace64.lib")) {
            return (Resolve-Path $root).Path
        }
    }
    return $null
}

function Resolve-AdsSapSdkDir {
    param([string]$Override = "")
    if ($Override) { return (Resolve-Path $Override).Path }
    if ($env:OPENADS_ADS_SAP_SDK_DIR) {
        return (Resolve-Path $env:OPENADS_ADS_SAP_SDK_DIR).Path
    }
    throw "SAP ADS SDK dir not found. Set OPENADS_ADS_SAP_SDK_DIR to your Advantage Database Server install (collate / icu .dat / cfg support files)."
}

function Resolve-AdsSapDllDir {
    param([string]$Override = "")
    if ($Override) { return (Resolve-Path $Override).Path }
    if ($env:OPENADS_ADS_SAP_DLL_DIR) {
        return (Resolve-Path $env:OPENADS_ADS_SAP_DLL_DIR).Path
    }
    $hb = Get-HarbourInstall
    if ($hb) {
        foreach ($sub in @("lib", "bin")) {
            $dir = Join-Path $hb $sub
            if (Test-Path (Join-Path $dir "ace64.dll")) {
                return (Resolve-Path $dir).Path
            }
        }
    }
    throw "SAP ace64.dll not found. Set OPENADS_ADS_SAP_DLL_DIR. Link lib: Harbour contrib/rddads (ace64.lib); runtime DLL: your Advantage redistribute."
}

function Resolve-AdsOpenDllDir {
    param([string]$Override = "")
    if ($Override) { return (Resolve-Path $Override).Path }
    if ($env:OPENADS_ADS_OPEN_DLL_DIR) {
        return (Resolve-Path $env:OPENADS_ADS_OPEN_DLL_DIR).Path
    }
    if ($env:OPENADS_DLL_SRC) {
        return (Resolve-Path $env:OPENADS_DLL_SRC).Path
    }

    $repo = Get-OpenAdsRepoRoot
    $candidates = @(
        (Join-Path $repo "examples\fivewin"),
        (Join-Path $repo "tests\smoke\harbour"),
        (Join-Path $repo "build\ninja\src"),
        (Join-Path $repo "build\sqlite-msvc\src")
    )
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir "openace64.dll")) {
            return (Resolve-Path $dir).Path
        }
    }
    throw "openace64.dll not found (set OPENADS_ADS_OPEN_DLL_DIR or OPENADS_DLL_SRC)."
}

function Resolve-AdsEngineLibPaths {
    $aceLib = $env:ACE64_LIB_PATH
    if (-not $aceLib) {
        $hb = Get-HarbourInstall
        if ($hb) {
            $libDir = Join-Path $hb "lib"
            if (Test-Path (Join-Path $libDir "ace64.lib")) { $aceLib = $libDir }
        }
    }
    if (-not $aceLib) { throw "Harbour ace64.lib not found (set ACE64_LIB_PATH or HB_INSTALL)." }

    $openLib = $env:OPENACE64_LIB
    $openLibDir = $env:OPENACE64_LIB_PATH
    if (-not $openLib) { throw "openace64.lib not found (set OPENACE64_LIB)." }
    if (-not $openLibDir) { $openLibDir = Split-Path -Parent $openLib }

    return @{
        AceLibDir   = (Resolve-Path $aceLib).Path
        OpenLib     = (Resolve-Path $openLib).Path
        OpenLibDir  = (Resolve-Path $openLibDir).Path
    }
}

function Get-OpenAdsPython {
    if ($env:OPENADS_PYTHON -and (Test-Path $env:OPENADS_PYTHON)) {
        return $env:OPENADS_PYTHON
    }
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command python3 -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Copy-AdsSupportFiles {
    param(
        [string]$DestDir,
        [string]$SdkDir
    )
    New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    $files = @(
        "icudt40l.dat", "adscollate.adm", "adscollate.adt",
        "ansi.chr", "extend.chr", "adslocal.cfg"
    )
    foreach ($name in $files) {
        $src = Join-Path $SdkDir $name
        if (-not (Test-Path $src)) {
            $src = Join-Path $SdkDir "acesdk\Redistribute\$name"
        }
        if (Test-Path $src) {
            Copy-Item $src (Join-Path $DestDir $name) -Force
        }
    }
}

function Parse-EngineBenchLog {
    param([string]$LogText)
    $rows = @()
    foreach ($line in ($LogText -split "`n")) {
        $line = $line.Trim()
        if ($line -match '^ENGINE_ROW,engine=([^,]+),workload=([^,]+),median_ms=([0-9.+-]+)') {
            $rows += [ordered]@{
                engine     = $Matches[1]
                workload   = $Matches[2]
                median_ms  = [double]$Matches[3]
            }
        }
    }
    return $rows
}

function Write-AdsCompareJson {
    param(
        [string]$OutPath,
        [hashtable]$Payload
    )
    $dir = Split-Path -Parent $OutPath
    if ($dir) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $ordered = [ordered]@{}
    foreach ($k in $Payload.Keys) { $ordered[$k] = $Payload[$k] }
    $ordered | ConvertTo-Json -Depth 6 | Set-Content -Path $OutPath -Encoding UTF8
}
