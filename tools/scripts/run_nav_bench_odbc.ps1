<#
  NAV bench timed (Harbour) + JSON + PDF — ODBC backend (portable).
  Usage: pwsh tools/scripts/run_nav_bench_odbc.ps1 [-Iters 30] [-Warmup 1]
  Env: OPENADS_ODBC_FIXTURE, OPENADS_ODBC_DRIVER, OPENADS_DLL_SRC, FIREBIRD (optional)
#>
param(
    [int]$Iters = 30,
    [int]$Warmup = 1,
    [string]$OdbcSrc = "",
    [string]$Fixture = "",
    [string]$DriverName = ""
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "nav_bench_common.ps1")

$adoRoot = Get-OpenAdsRepoRoot
$demo = Join-Path $adoRoot "examples\fivewin"
$resultsDir = Join-Path $adoRoot "tools\bench\results"
$jsonOut = Join-Path $resultsDir "nav_bench_odbc_latest.json"
$logOut = Join-Path $resultsDir "nav_bench_odbc_latest.log"
$genPdf = Join-Path $adoRoot "tools\scripts\gen_nav_bench_pdf.py"
$py = Get-OpenAdsPython

$fbDb = $Fixture
if (-not $fbDb) { $fbDb = $env:OPENADS_ODBC_FIXTURE }
if (-not $fbDb -or -not (Test-Path $fbDb)) {
    Write-Error "Firebird fixture missing. Set -Fixture or OPENADS_ODBC_FIXTURE"
    exit 1
}

$driverName = $DriverName
if (-not $driverName) { $driverName = $env:OPENADS_ODBC_DRIVER }
if (-not $driverName) {
    $drivers = Get-OdbcDriver -Platform "64-bit" -ErrorAction SilentlyContinue
    if ($drivers.Name -contains "Firebird ODBC Driver") {
        $driverName = "Firebird ODBC Driver"
    } else {
        Write-Error "ODBC driver not found. Set OPENADS_ODBC_DRIVER"
        exit 1
    }
}

$uid = if ($env:OPENADS_ODBC_UID) { $env:OPENADS_ODBC_UID } else { "SYSDBA" }
$pwd = $env:OPENADS_ODBC_PWD
$oadll = Resolve-OpenAdsDllDir -Backend odbc -Override $OdbcSrc
$connstr = "Driver={$driverName};Database=$fbDb;Uid=$uid;Pwd=$pwd;Charset=UTF8;"
$env:OPENADS_TEST_ODBC_CONNSTR = $connstr
$env:OPENADS_NAV_MODE = "odbc"
$env:OPENADS_NAV_BENCH_ITERS = "$Iters"
$env:OPENADS_NAV_BENCH_WARMUP = "$Warmup"
$pathExtra = $oadll
if ($env:FIREBIRD) { $pathExtra = "$oadll;$($env:FIREBIRD)" }
$env:PATH = "$pathExtra;$env:PATH"

$run = Invoke-NavBenchDemo -DemoDir $demo -Mode odbc -Iters $Iters -Warmup $Warmup -OaDll $oadll -LogPath $logOut
$stdout = Get-Content $logOut -Raw -ErrorAction SilentlyContinue
if (-not $stdout) { $stdout = "" }

Write-NavBenchJson -OutPath $jsonOut -Payload @{
    timestamp = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    branch    = (Get-OpenAdsGitBranch $adoRoot)
    mode      = "odbc"
    iters     = $Iters
    warmup    = $Warmup
    exit_code = $run.ExitCode
    wall_ms   = $run.WallMs
    fixture   = (Resolve-Path $fbDb).Path
    oadll     = $oadll
    stdout    = $stdout
}

Write-Host "[bench] exit=$($run.ExitCode) wall_ms=$($run.WallMs) json=$jsonOut"
if ($run.ExitCode -ne 0) { exit $run.ExitCode }
if (-not $py) {
    Write-Error "python not found (required for PDF)"
    exit 1
}
& $py $genPdf $jsonOut
exit $LASTEXITCODE