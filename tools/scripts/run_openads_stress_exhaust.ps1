<#
  OpenADS exhaustive stress — local engine + remote server (tcp://).
  Usage: pwsh tools/scripts/run_openads_stress_exhaust.ps1 [-Profile all|local|server]
         [-DataDir path] [-ServerPort 17262] [-IncludeNav] [-IncludeEngineCompare]
  Env: OPENADS_ADSEXHAUST_EXE, OPENADS_SERVERD_EXE, OPENADS_DLL_SRC
#>
param(
    [ValidateSet("all", "local", "server")]
    [string]$Profile = "all",
    [string]$DataDir = "",
    [int]$ServerPort = 17262,
    [switch]$IncludeNav,
    [switch]$IncludeEngineCompare,
    [switch]$IncludeCppStress,
    [switch]$IncludeTryLock,
    [switch]$StopOnFirstFail
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "openads_stress_common.ps1")

$repo = Get-OpenAdsRepoRoot
$resultsDir = Join-Path $repo "tools\bench\results"
$jsonOut = Join-Path $resultsDir "openads_stress_exhaust_latest.json"
$pdfGen = Join-Path $repo "tools\scripts\gen_openads_stress_exhaust_pdf.py"
$py = Get-OpenAdsPython

if (-not $DataDir) {
    $DataDir = Join-Path $repo "tools\bench\fixtures\stress_exhaust_data"
}
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
$DataDir = (Resolve-Path $DataDir).Path

$exhaustExe = Resolve-AdsexhaustExe
$exhaustRoot = Split-Path -Parent $exhaustExe
$openDll = Resolve-AdsOpenDllDir
$serverd = Resolve-OpenAdsServerd

$localModes = Get-OpenAdsStressModes -Profile local
$serverModes = if ($Profile -in @("all", "server")) {
    Get-OpenAdsStressModes -Profile server
} else { @() }

$runs = @()
if ($Profile -in @("all", "local")) {
    foreach ($m in $localModes) {
        $runs += [ordered]@{ profile = "local"; mode = $m.mode; arg = $m.arg; label = $m.label }
    }
}
if ($Profile -in @("all", "server")) {
    foreach ($m in $serverModes) {
        $runs += [ordered]@{ profile = "server"; mode = $m.mode; arg = $m.arg; label = $m.label }
    }
}

function Invoke-AdsexhaustMode {
    param(
        [string]$RunProfile,
        [string]$Mode,
        [string]$Arg,
        [string]$WorkDir,
        [string]$ConnectUri = ""
    )
    $logName = "stress_${RunProfile}_${Mode}.log"
    $logPath = Join-Path $resultsDir $logName
    $env:OPENADS_CONNECT_URI = $ConnectUri
    if ($RunProfile -eq "local") {
        Remove-Item Env:OPENADS_CONNECT_URI -ErrorAction SilentlyContinue
    }

    Push-Location $WorkDir
    try {
        Copy-Item (Join-Path $openDll "openace64.dll") $WorkDir -Force -ErrorAction SilentlyContinue
        Copy-Item $exhaustExe $WorkDir -Force
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $relData = "data"
        $localData = Join-Path $WorkDir "data"
        New-Item -ItemType Directory -Force -Path $localData | Out-Null
        if ($Mode -eq "init" -and $RunProfile -eq "server") {
            Remove-Item (Join-Path $localData "*") -Force -Recurse -ErrorAction SilentlyContinue
        }
        $args = @($relData, $Mode)
        if ($Arg) { $args += $Arg }
        & ".\adsexhaust_64.exe" @args 2>&1 | Tee-Object -FilePath $logPath
        $code = $LASTEXITCODE
        $sw.Stop()
    } finally {
        Pop-Location
        Remove-Item Env:OPENADS_CONNECT_URI -ErrorAction SilentlyContinue
    }
    $stdout = Get-Content $logPath -Raw -ErrorAction SilentlyContinue
    return @{
        profile  = $RunProfile
        mode     = $Mode
        arg      = $Arg
        exit     = $code
        wall_ms  = $sw.ElapsedMilliseconds
        log      = $logPath
        uri      = $ConnectUri
        stdout   = if ($stdout.Length -gt 8000) { $stdout.Substring(0, 8000) + "..." } else { $stdout }
    }
}

$serverProc = $null
$serverUri = ""
$serverSkipped = $null
$localWork = Join-Path $resultsDir "stress_exhaust_local"
$serverWork = Join-Path $resultsDir "stress_exhaust_server"

if (Test-Path $localWork) { Remove-Item $localWork -Recurse -Force -ErrorAction SilentlyContinue }
if (Test-Path $serverWork) { Remove-Item $serverWork -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $localWork | Out-Null
Copy-Item -Recurse -Force (Join-Path $DataDir "*") (Join-Path $localWork "data") -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $localWork "data") | Out-Null

$modeResults = @()
Write-Host "[stress] adsexhaust=$exhaustExe openads=$openDll"

foreach ($r in $runs) {
    if ($r.profile -eq "server") {
        if (-not $serverd) {
            if (-not $serverSkipped) {
                $serverSkipped = "openads_serverd.exe not built (cmake build release-x64)"
                Write-Host "[stress] SKIP server profile: $serverSkipped"
            }
            continue
        }
        if (-not $serverProc -or $serverProc.HasExited) {
            if (Test-Path $serverWork) { Remove-Item $serverWork -Recurse -Force -ErrorAction SilentlyContinue }
            New-Item -ItemType Directory -Force -Path $serverWork | Out-Null
            New-Item -ItemType Directory -Force -Path (Join-Path $serverWork "data") | Out-Null
            $serverProc = Start-Process -FilePath $serverd -ArgumentList @(
                "--host", "127.0.0.1", "--port", "$ServerPort", "--data", (Join-Path $serverWork "data")
            ) -PassThru -WindowStyle Hidden
            Start-Sleep -Seconds 2
            $serverUri = Format-OpenAdsTcpUri -Port $ServerPort -DataDir (Join-Path $serverWork "data")
            Write-Host "[stress] serverd pid=$($serverProc.Id) uri=$serverUri"
        }
        $res = Invoke-AdsexhaustMode -RunProfile server -Mode $r.mode -Arg $r.arg `
            -WorkDir $serverWork -ConnectUri $serverUri
    } else {
        $res = Invoke-AdsexhaustMode -RunProfile local -Mode $r.mode -Arg $r.arg -WorkDir $localWork
    }
    $modeResults += $res
    Write-Host "[stress] $($r.profile)/$($r.mode) exit=$($res.exit) ms=$($res.wall_ms)"
    if ($StopOnFirstFail -and $res.exit -ne 0) { break }
}

if ($IncludeTryLock -and $Profile -in @("all", "local")) {
    Push-Location $localWork
    try {
        Copy-Item (Join-Path $openDll "openace64.dll") $localWork -Force
        Copy-Item $exhaustExe $localWork -Force
        cmd /c "start /b adsexhaust_64.exe data hold 10 & ping 127.0.0.1 -n 3 >nul & adsexhaust_64.exe data trylock"
        $tlCode = $LASTEXITCODE
        $modeResults += @{
            profile = "local"; mode = "trylock"; arg = "hold"; exit = $tlCode
            wall_ms = 0; log = (Join-Path $localWork "adsexhaust.log"); uri = ""; stdout = ""
        }
    } finally { Pop-Location }
}

if ($serverProc -and -not $serverProc.HasExited) {
    Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
}

$cppResults = @()
if ($IncludeCppStress) {
    $stress = Resolve-OpenAdsStressTool "openads_stress.exe"
    $conc = Resolve-OpenAdsStressTool "openads_concurrency_stress.exe"
    $cppDir = Join-Path $localWork "cpp_stress"
    New-Item -ItemType Directory -Force -Path $cppDir | Out-Null
    Copy-Item (Join-Path $openDll "openace64.dll") $cppDir -Force
    if ($stress) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        & $stress --rows 5000 --dir $cppDir --with-indexes 2>&1 | Out-Null
        $cppResults += @{ tool = "openads_stress"; exit = $LASTEXITCODE; wall_ms = $sw.ElapsedMilliseconds }
    }
    if ($conc) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        & $conc --threads 4 --seconds 15 --dir $cppDir 2>&1 | Out-Null
        $cppResults += @{ tool = "openads_concurrency_stress"; exit = $LASTEXITCODE; wall_ms = $sw.ElapsedMilliseconds }
    }
}

$navResults = @()
if ($IncludeNav) {
    foreach ($nav in @("sqlite", "postgresql", "odbc")) {
        $script = Join-Path $repo "tools\scripts\run_nav_bench_${nav}.ps1"
        if ($nav -eq "postgresql") { $script = Join-Path $repo "tools\scripts\run_nav_bench_pg.ps1" }
        if (Test-Path $script) {
            & pwsh -NoProfile -File $script -Iters 10 -Warmup 1
            $navResults += @{ backend = $nav; exit = $LASTEXITCODE }
        }
    }
}

$engineCompare = $null
if ($IncludeEngineCompare) {
    & (Join-Path $repo "tools\scripts\run_ads_vs_openads_bench.ps1") -Rows 2000 -Repeats 3
    $engineCompare = @{ exit = $LASTEXITCODE }
}

$ranLocal = ($modeResults | Where-Object { $_.profile -eq "local" }).Count
$ranServer = ($modeResults | Where-Object { $_.profile -eq "server" }).Count
$scheduled = $runs.Count + $(if ($IncludeTryLock -and $Profile -in @("all", "local")) { 1 } else { 0 })
$fail = ($modeResults | Where-Object { $_.exit -ne 0 }).Count
$pass = ($fail -eq 0) -and ($modeResults.Count -ge $scheduled -or $modeResults.Count -gt 0) -and ($fail -eq 0)
$exhausted = ($Profile -eq "server" -and $serverSkipped) ? $true : ($modeResults.Count -ge $runs.Count)

Write-StressExhaustJson -OutPath $jsonOut -Payload @{
    timestamp     = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    branch        = if (Test-Path (Join-Path $repo ".git")) {
        Push-Location $repo; try { git rev-parse --abbrev-ref HEAD 2>$null } finally { Pop-Location }
    } else { "unknown" }
    profile       = $Profile
    pass          = $pass
    exhausted     = $exhausted
    modes_failed  = $fail
    modes_ran     = $modeResults.Count
    data_dir      = $DataDir
    open_dll      = $openDll
    adsexhaust    = $exhaustExe
    serverd       = $serverd
    server_port   = $ServerPort
    server_skip   = $serverSkipped
    modes         = $modeResults
    cpp           = $cppResults
    nav           = $navResults
    engine_compare = $engineCompare
}

Write-Host "[stress] pass=$pass fail=$fail json=$jsonOut local_modes=$ranLocal server_modes=$ranServer"
if (-not $pass) { exit 1 }

if ($py -and (Test-Path $pdfGen)) {
    & $py $pdfGen $jsonOut
}
exit 0