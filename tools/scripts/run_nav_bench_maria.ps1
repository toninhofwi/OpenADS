<#
  NAV bench — MariaDB/MySQL backend (mariadb://).
  Usage: pwsh tools/scripts/run_nav_bench_maria.ps1 [-Iters 30]
  Env: OPENADS_TEST_MARIADB_URI, OPENADS_MYSQL_BIN (folder with mysql.exe)
#>
param(
    [int]$Iters = 30,
    [int]$Warmup = 1,
    [string]$MariaSrc = "",
    [string]$MariaUri = ""
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "nav_bench_common.ps1")

$adoRoot = Get-OpenAdsRepoRoot
$demo = Join-Path $adoRoot "examples\fivewin"
$resultsDir = Join-Path $adoRoot "tools\bench\results"
$jsonOut = Join-Path $resultsDir "nav_bench_maria_latest.json"
$logOut = Join-Path $resultsDir "nav_bench_maria_latest.log"
$genPdf = Join-Path $adoRoot "tools\scripts\gen_nav_bench_pdf.py"
$seedSql = Join-Path $adoRoot "tools\scripts\seed_nav_clientes_maria.sql"
$py = Get-OpenAdsPython

if (-not $MariaUri) { $MariaUri = $env:OPENADS_TEST_MARIADB_URI }
if (-not $MariaUri) { $MariaUri = "mariadb://root@127.0.0.1:3306/test" }

$oadll = Resolve-OpenAdsDllDir -Backend mariadb -Override $MariaSrc

$mysqlBin = $env:OPENADS_MYSQL_BIN
if (-not $mysqlBin) {
    $cmd = Get-Command mysql -ErrorAction SilentlyContinue
    if ($cmd) { $mysqlBin = Split-Path -Parent $cmd.Source }
}
if ($mysqlBin -and (Test-Path (Join-Path $mysqlBin "mysql.exe")) -and (Test-Path $seedSql)) {
    Write-Host "[maria] seed fixture clientes..."
    Get-Content $seedSql -Raw | & (Join-Path $mysqlBin "mysql.exe") -h 127.0.0.1 -u root test 2>&1 | Out-Host
} else {
    Write-Warning "[maria] mysql seed skipped — ensure clientes table exists"
}

$env:OPENADS_TEST_MARIADB_URI = $MariaUri
$env:OPENADS_NAV_MODE = "mariadb"
$env:OPENADS_NAV_BENCH_ITERS = "$Iters"
$env:OPENADS_NAV_BENCH_WARMUP = "$Warmup"
$env:PATH = "$oadll;$env:PATH"

$run = Invoke-NavBenchDemo -DemoDir $demo -Mode maria -Iters $Iters -Warmup $Warmup -OaDll $oadll -LogPath $logOut
$stdout = Get-Content $logOut -Raw -ErrorAction SilentlyContinue
if (-not $stdout) { $stdout = "" }

Write-NavBenchJson -OutPath $jsonOut -Payload @{
    timestamp  = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    branch     = (Get-OpenAdsGitBranch $adoRoot)
    mode       = "mariadb"
    iters      = $Iters
    warmup     = $Warmup
    exit_code  = $run.ExitCode
    wall_ms    = $run.WallMs
    maria_uri  = $MariaUri
    oadll      = $oadll
    stdout     = $stdout
}

Write-Host "[bench] exit=$($run.ExitCode) wall_ms=$($run.WallMs) json=$jsonOut"
if ($run.ExitCode -ne 0) { exit $run.ExitCode }
if (-not $py) {
    Write-Error "python not found (required for PDF)"
    exit 1
}
& $py $genPdf $jsonOut
exit $LASTEXITCODE