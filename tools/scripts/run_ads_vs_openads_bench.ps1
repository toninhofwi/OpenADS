<#
  SAP ADS vs OpenADS engine A/B bench (local DBF/CDX).
  Usage: pwsh tools/scripts/run_ads_vs_openads_bench.ps1 [-Rows 10000] [-Repeats 3] [-Build]
  Env: OPENADS_ADS_SAP_SDK_DIR (Advantage SDK), OPENADS_ADS_SAP_DLL_DIR (ace64),
       OPENADS_ADS_OPEN_DLL_DIR, OPENADS_HB_BIN, OPENADS_MSVC_SETUP
#>
param(
    [int]$Rows = 10000,
    [int]$Repeats = 3,
    [switch]$Build,
    [switch]$Rdd = $true,
    [string]$DataDir = "",
    [string]$SapSdkDir = "",
    [string]$SapDllDir = "",
    [string]$OpenDllDir = ""
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ads_bench_common.ps1")

$repo = Get-OpenAdsRepoRoot
$compare = Join-Path $repo "tools\bench\ads_compare"
$bin = Join-Path $compare "bin"
$results = Join-Path $repo "tools\bench\results"
$jsonOut = Join-Path $results "ads_vs_openads_latest.json"
$pdfGen = Join-Path $repo "tools\scripts\gen_ads_compare_pdf.py"
$py = Get-OpenAdsPython

if (-not $DataDir) {
    $DataDir = Join-Path $repo "tools\bench\fixtures\ads_compare_data"
}
& (Join-Path $PSScriptRoot "prepare_ads_compare_data.ps1") -DestDir $DataDir -Rows $Rows -Build:$Build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$DataDir = (Resolve-Path $DataDir).Path

$sapSdk = Resolve-AdsSapSdkDir -Override $SapSdkDir
$sapDll = Resolve-AdsSapDllDir -Override $SapDllDir
$openDll = Resolve-AdsOpenDllDir -Override $OpenDllDir
$libs = Resolve-AdsEngineLibPaths

# Toolchain locations (OPENADS_HB_BIN / OPENADS_MSVC_SETUP / OPENADS_MSVC_UCRT)
# are taken from the environment; build_engine_bench.bat validates them.
$env:ACE64_LIB_PATH = $libs.AceLibDir
$env:OPENACE64_LIB = $libs.OpenLib
$env:OPENACE64_LIB_PATH = $libs.OpenLibDir
if ($env:OPENADS_MSVC_UCRT) {
    $env:MSVC_UCRT_X64 = $env:OPENADS_MSVC_UCRT
}

$sapExe = Join-Path $bin "ads_engine_sap_64.exe"
$openExe = Join-Path $bin "ads_engine_openads_64.exe"
if ($Build -or -not (Test-Path $sapExe) -or -not (Test-Path $openExe)) {
    Write-Host "[build] ads_engine bench exes..."
    Push-Location $compare
    try {
        & cmd /c build_engine_bench.bat
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        Pop-Location
    }
}

function Invoke-EngineRun {
    param(
        [string]$Tag,
        [string]$ExePath,
        [string]$EngineDllDir,
        [string]$DllName
    )
    $runDir = Join-Path $results "ads_compare_run_$Tag"
    if (Test-Path $runDir) { Remove-Item $runDir -Recurse -Force }
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null

    $localData = Join-Path $runDir "data"
    New-Item -ItemType Directory -Path $localData -Force | Out-Null
    Copy-Item (Join-Path $DataDir "bench.dbf") $localData -Force
    Remove-Item (Join-Path $localData "bench.cdx") -Force -ErrorAction SilentlyContinue

    Copy-Item $ExePath $runDir -Force
    Copy-AdsSupportFiles -DestDir $runDir -SdkDir $sapSdk
    Get-ChildItem $EngineDllDir -Filter "*.dll" | Copy-Item -Destination $runDir -Force

    $logPath = Join-Path $runDir "ads_engine_$Tag.log"
    $rddArg = if ($Rdd) { "RDD" } else { "" }
    $env:ADS_ENGINE_NAME = $Tag
    $env:ADS_ENGINE_LOG = $logPath

    Push-Location $runDir
    try {
        $exeName = Split-Path $ExePath -Leaf
        $args = @($localData, "$Rows", "$Repeats")
        if ($rddArg) { $args += $rddArg }
        & ".\$exeName" @args 2>&1 | Tee-Object -FilePath $logPath
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    $logText = Get-Content $logPath -Raw -ErrorAction SilentlyContinue
    if (-not $logText) { $logText = "" }
    return @{
        engine   = $Tag
        exit     = $code
        log      = $logPath
        dll_dir  = $EngineDllDir
        dll      = $DllName
        rows     = (Parse-EngineBenchLog $logText)
        stdout   = $logText
    }
}

Write-Host "[bench] SAP sdk=$sapSdk dll=$sapDll"
Write-Host "[bench] OpenADS dll=$openDll data=$DataDir"

$sapRun = Invoke-EngineRun -Tag "sap" -ExePath $sapExe -EngineDllDir $sapDll -DllName "ace64.dll"
$openRun = Invoke-EngineRun -Tag "openads" -ExePath $openExe -EngineDllDir $openDll -DllName "openace64.dll"

$comparison = @()
$allWorkloads = ($sapRun.rows + $openRun.rows | ForEach-Object { $_.workload } | Select-Object -Unique)
foreach ($wl in $allWorkloads) {
    $sapMs = ($sapRun.rows | Where-Object { $_.workload -eq $wl } | Select-Object -First 1).median_ms
    $openMs = ($openRun.rows | Where-Object { $_.workload -eq $wl } | Select-Object -First 1).median_ms
    $ratio = $null
    if ($sapMs -and $openMs -and $sapMs -gt 0) {
        $ratio = [math]::Round($openMs / $sapMs, 3)
    }
    $comparison += [ordered]@{
        workload   = $wl
        sap_ms     = $sapMs
        openads_ms = $openMs
        ratio      = $ratio
    }
}

$sapOk = ($sapRun.exit -eq 0) -and ($sapRun.rows.Count -gt 0) -and (
    ($sapRun.rows | Where-Object { $_.median_ms -gt 0 }).Count -gt 0
)
$openOk = ($openRun.exit -eq 0) -and ($openRun.rows.Count -gt 0)
$pass = $sapOk -and $openOk -and ($comparison.Count -gt 0)

Write-AdsCompareJson -OutPath $jsonOut -Payload @{
    timestamp    = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    branch       = if (Test-Path (Join-Path $repo ".git")) {
        Push-Location $repo; try { git rev-parse --abbrev-ref HEAD 2>$null } finally { Pop-Location }
    } else { "unknown" }
    rows         = $Rows
    repeats      = $Repeats
    rdd          = [bool]$Rdd
    pass         = $pass
    sap_sdk_dir  = $sapSdk
    sap_dll_dir  = $sapDll
    open_dll_dir = $openDll
    data_dir     = $DataDir
    sap          = @{
        engine  = $sapRun.engine
        exit    = $sapRun.exit
        log     = $sapRun.log
        dll_dir = $sapRun.dll_dir
        rows    = $sapRun.rows
    }
    openads      = @{
        engine  = $openRun.engine
        exit    = $openRun.exit
        log     = $openRun.log
        dll_dir = $openRun.dll_dir
        rows    = $openRun.rows
    }
    comparison   = $comparison
}

Write-Host "[bench] pass=$pass json=$jsonOut sap_exit=$($sapRun.exit) openads_exit=$($openRun.exit)"
if (-not $pass) { exit 1 }

if ($py -and (Test-Path $pdfGen)) {
    & $py $pdfGen $jsonOut
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit 0