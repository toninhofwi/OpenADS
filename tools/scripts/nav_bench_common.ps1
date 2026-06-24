# Shared helpers for NAV bench runners (portable — no drive letter hardcoded).
# Repo root = parent of tools/. Optional sibling worktrees via OPENADS_WORKTREE_ROOT.

function Get-OpenAdsRepoRoot {
    if ($env:OPENADS_REPO_ROOT) {
        return (Resolve-Path $env:OPENADS_REPO_ROOT).Path
    }
    $here = Split-Path -Parent $PSScriptRoot
    return (Resolve-Path (Join-Path $here "..")).Path
}

function Get-OpenAdsWorktreeRoot {
    if ($env:OPENADS_WORKTREE_ROOT) {
        return (Resolve-Path $env:OPENADS_WORKTREE_ROOT).Path
    }
    return (Split-Path -Parent (Get-OpenAdsRepoRoot))
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

function Resolve-OpenAdsDllDir {
    param(
        [ValidateSet("sqlite", "postgresql", "odbc", "mariadb")]
        [string]$Backend,
        [string]$Override = ""
    )
    if ($Override) {
        return (Resolve-Path $Override).Path
    }
    if ($env:OPENADS_DLL_SRC) {
        return (Resolve-Path $env:OPENADS_DLL_SRC).Path
    }

    $repo = Get-OpenAdsRepoRoot
    $wt = Get-OpenAdsWorktreeRoot
    $candidates = switch ($Backend) {
        "sqlite" {
            @(
                (Join-Path $wt "OpenADS-sqlpass\build\ninja\src"),
                (Join-Path $wt "OpenADS-sqlpass\build\sqlite-msvc\src"),
                (Join-Path $repo "build\ninja\src"),
                (Join-Path $repo "build\sqlite-msvc\src")
            )
        }
        "postgresql" {
            @(
                (Join-Path $wt "OpenADS-postgresql\build\pg-msvc\src"),
                (Join-Path $wt "OpenADS-postgresql\build\pg\src"),
                (Join-Path $repo "build\pg-msvc\src"),
                (Join-Path $repo "build\pg\src")
            )
        }
        "odbc" {
            @(
                (Join-Path $wt "OpenADS-odbc\build\odbc-msvc\src"),
                (Join-Path $repo "build\odbc-msvc\src")
            )
        }
        "mariadb" {
            @(
                (Join-Path $wt "OpenADS-mysql\build\maria-msvc\src"),
                (Join-Path $repo "build\maria-msvc\src")
            )
        }
    }
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir "openace64.dll")) {
            return (Resolve-Path $dir).Path
        }
    }
    throw "openace64.dll not found for backend '$Backend'. Set OPENADS_DLL_SRC or build the backend."
}

function Get-OpenAdsGitBranch {
    param([string]$RepoRoot)
    if (-not (Test-Path (Join-Path $RepoRoot ".git"))) { return "unknown" }
    Push-Location $RepoRoot
    try { return (git rev-parse --abbrev-ref HEAD 2>$null) } finally { Pop-Location }
}

function Write-NavBenchJson {
    param(
        [string]$OutPath,
        [hashtable]$Payload
    )
    $dir = Split-Path -Parent $OutPath
    if ($dir) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $ordered = [ordered]@{}
    foreach ($k in $Payload.Keys) { $ordered[$k] = $Payload[$k] }
    $ordered | ConvertTo-Json -Depth 4 | Set-Content -Path $OutPath -Encoding UTF8
}

function Invoke-NavBenchDemo {
    param(
        [string]$DemoDir,
        [string]$Mode,
        [int]$Iters,
        [int]$Warmup,
        [string]$OaDll,
        [string]$LogPath
    )
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Push-Location $DemoDir
    try {
        New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force | Out-Null
        & cmd /c "demo_nav_bench.bat $Mode $Iters $Warmup `"$OaDll`"" 2>&1 | Tee-Object -FilePath $LogPath
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $sw.Stop()
    return @{ ExitCode = $code; WallMs = [int]$sw.ElapsedMilliseconds }
}

function Format-SqliteUri {
    param([string]$DbPath)
    $p = (Resolve-Path $DbPath).Path -replace '\\', '/'
    if ($p -match '^[A-Za-z]:/') {
        return "sqlite:///$p"
    }
    return "sqlite://$p"
}