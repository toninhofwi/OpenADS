<#
  NAV bench — SQL Server via ODBC (Enterprise driver).
  Usage: pwsh tools/scripts/run_nav_bench_mssql.ps1 [-Iters 30]
  Env: OPENADS_MSSQL_ODBC_CONNSTR or OPENADS_MSSQL_SERVER + OPENADS_MSSQL_DATABASE
#>
param(
    [int]$Iters = 30,
    [int]$Warmup = 1,
    [string]$OdbcSrc = "",
    [string]$ConnStr = "",
    [string]$Server = "",
    [string]$Database = "master"
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "nav_bench_common.ps1")

$adoRoot = Get-OpenAdsRepoRoot
$demo = Join-Path $adoRoot "examples\fivewin"
$resultsDir = Join-Path $adoRoot "tools\bench\results"
$jsonOut = Join-Path $resultsDir "nav_bench_mssql_latest.json"
$logOut = Join-Path $resultsDir "nav_bench_mssql_latest.log"
$genPdf = Join-Path $adoRoot "tools\scripts\gen_nav_bench_pdf.py"
$seedSql = Join-Path $adoRoot "tools\scripts\seed_nav_clientes_mssql.sql"
$py = Get-OpenAdsPython

if (-not $ConnStr) { $ConnStr = $env:OPENADS_MSSQL_ODBC_CONNSTR }
if (-not $ConnStr) {
    $srv = if ($Server) { $Server } else { $env:OPENADS_MSSQL_SERVER }
    if (-not $srv) { $srv = "localhost" }
    $db = if ($env:OPENADS_MSSQL_DATABASE) { $env:OPENADS_MSSQL_DATABASE } else { $Database }
    $driver = $env:OPENADS_MSSQL_ODBC_DRIVER
    if (-not $driver) {
        $drivers = Get-OdbcDriver -Platform "64-bit" -ErrorAction SilentlyContinue
        $pick = $drivers.Name | Where-Object { $_ -match 'SQL Server' } | Select-Object -First 1
        if ($pick) { $driver = $pick } else { $driver = "SQL Server" }
    }
    $ConnStr = "Driver={$driver};Server=$srv;Database=$db;Trusted_Connection=yes;"
}

$oadll = Resolve-OpenAdsDllDir -Backend odbc -Override $OdbcSrc

# Seed via sqlcmd if available, else skip with warning
$sqlcmd = Get-Command sqlcmd -ErrorAction SilentlyContinue
if ($sqlcmd -and (Test-Path $seedSql)) {
    Write-Host "[mssql] seed fixture clientes..."
    $srvName = if ($env:OPENADS_MSSQL_SERVER) { $env:OPENADS_MSSQL_SERVER } else { "localhost" }
    $dbName = if ($env:OPENADS_MSSQL_DATABASE) { $env:OPENADS_MSSQL_DATABASE } else { $Database }
    & sqlcmd -S $srvName -d $dbName -E -b -i $seedSql 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Warning "[mssql] sqlcmd/seed skipped — ensure dbo.clientes exists before bench"
}

$env:OPENADS_TEST_ODBC_CONNSTR = $ConnStr
$env:OPENADS_NAV_MODE = "odbc"
$env:OPENADS_NAV_BENCH_ITERS = "$Iters"
$env:OPENADS_NAV_BENCH_WARMUP = "$Warmup"
$env:PATH = "$oadll;$env:PATH"

$run = Invoke-NavBenchDemo -DemoDir $demo -Mode odbc -Iters $Iters -Warmup $Warmup -OaDll $oadll -LogPath $logOut
$stdout = Get-Content $logOut -Raw -ErrorAction SilentlyContinue
if (-not $stdout) { $stdout = "" }

Write-NavBenchJson -OutPath $jsonOut -Payload @{
    timestamp = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    branch    = (Get-OpenAdsGitBranch $adoRoot)
    mode      = "mssql"
    iters     = $Iters
    warmup    = $Warmup
    exit_code = $run.ExitCode
    wall_ms   = $run.WallMs
    connstr   = $ConnStr
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