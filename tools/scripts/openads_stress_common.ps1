# Shared helpers for OpenADS exhaustive stress (local + server).

. (Join-Path $PSScriptRoot "ads_bench_common.ps1")

function Get-OpenAdsStressModes {
    param(
        [ValidateSet("local", "server", "all")]
        [string]$Profile = "all"
    )
    $local = @(
        @{ mode = "init";   arg = "";  label = "init clean table" },
        @{ mode = "read";   arg = "";  label = "read RDD/SQL/AOF" },
        @{ mode = "write";  arg = "";  label = "append/update/delete" },
        @{ mode = "lock";   arg = "";  label = "flock/rlock single process" },
        @{ mode = "stress"; arg = "50"; label = "stress loop 50 iter" },
        @{ mode = "dbf";    arg = "";  label = "DBF/CDX/memo/reopen" },
        @{ mode = "tx";     arg = "";  label = "transactional suite" },
        @{ mode = "rel";    arg = "";  label = "relational JOIN SQL" },
        @{ mode = "pr";     arg = "";  label = "PR gate init+all+stress100" }
    )
    $server = @(
        @{ mode = "init";   arg = "";  label = "remote init" },
        @{ mode = "read";   arg = "";  label = "remote read suite" },
        @{ mode = "write";  arg = "";  label = "remote write suite" },
        @{ mode = "stress"; arg = "50"; label = "remote stress 50" },
        @{ mode = "pr";     arg = "";  label = "remote PR gate" }
    )
    switch ($Profile) {
        "local"  { return $local }
        "server" { return $server }
        default  { return @($local + $server) }
    }
}

function Resolve-AdsexhaustExe {
    if ($env:OPENADS_ADSEXHAUST_EXE -and (Test-Path $env:OPENADS_ADSEXHAUST_EXE)) {
        return (Resolve-Path $env:OPENADS_ADSEXHAUST_EXE).Path
    }
    $wt = if ($env:OPENADS_WORKTREE_ROOT) {
        (Resolve-Path $env:OPENADS_WORKTREE_ROOT).Path
    } else {
        Split-Path -Parent (Get-OpenAdsRepoRoot)
    }
    $candidates = @(
        (Join-Path $wt "adsexhaust\adsexhaust_64.exe"),
        (Join-Path $wt "OpenADS-ado\tools\bench\stress_exhaust\adsexhaust_64.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    throw "adsexhaust_64.exe not found (set OPENADS_ADSEXHAUST_EXE or build adsexhaust)."
}

function Resolve-OpenAdsServerd {
    if ($env:OPENADS_SERVERD_EXE -and (Test-Path $env:OPENADS_SERVERD_EXE)) {
        return (Resolve-Path $env:OPENADS_SERVERD_EXE).Path
    }
    $repo = Get-OpenAdsRepoRoot
    $candidates = @(
        (Join-Path $repo "build\release-x64\tools\serverd\Release\openads_serverd.exe"),
        (Join-Path $repo "build\msvc-x64\tools\serverd\Release\openads_serverd.exe"),
        (Join-Path $repo "build\ninja\tools\serverd\openads_serverd.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Resolve-OpenAdsStressTool {
    param([string]$Name)
    if ($env:OPENADS_STRESS_BIN_DIR) {
        $p = Join-Path $env:OPENADS_STRESS_BIN_DIR $Name
        if (Test-Path $p) { return (Resolve-Path $p).Path }
    }
    $repo = Get-OpenAdsRepoRoot
    $candidates = @(
        (Join-Path $repo "build\release-x64\tools\stress\Release\$Name"),
        (Join-Path $repo "build\msvc-x64\tools\stress\Release\$Name"),
        (Join-Path $repo "build\ninja\tools\stress\$Name")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Format-OpenAdsTcpUri {
    param(
        [string]$Host = "127.0.0.1",
        [int]$Port,
        [string]$DataDir
    )
    $path = (Resolve-Path $DataDir).Path -replace '\\', '/'
    if ($path -match '^([A-Za-z]):') {
        $path = '/' + $path
    }
    return "tcp://${Host}:${Port}/${path}"
}

function Write-StressExhaustJson {
    param([string]$OutPath, [hashtable]$Payload)
    $dir = Split-Path -Parent $OutPath
    if ($dir) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $ordered = [ordered]@{}
    foreach ($k in $Payload.Keys) { $ordered[$k] = $Payload[$k] }
    $ordered | ConvertTo-Json -Depth 8 | Set-Content -Path $OutPath -Encoding UTF8
}