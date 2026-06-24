<#
  NAV bench timed (Harbour) + JSON + PDF — PostgreSQL backend (portable).
  Usage: pwsh tools/scripts/run_nav_bench_pg.ps1 [-Iters 30] [-Warmup 1]
  Env: OPENADS_TEST_PG_URI, OPENADS_PSQL_BIN (folder with psql.exe), OPENADS_DLL_SRC
#>
param(
    [int]$Iters = 30,
    [int]$Warmup = 1,
    [string]$PgSrc = "",
    [string]$PgUri = "",
    [int]$PgPort = 0
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "nav_bench_common.ps1")

$adoRoot = Get-OpenAdsRepoRoot
$demo = Join-Path $adoRoot "examples\fivewin"
$resultsDir = Join-Path $adoRoot "tools\bench\results"
$jsonOut = Join-Path $resultsDir "nav_bench_pg_latest.json"
$logOut = Join-Path $resultsDir "nav_bench_pg_latest.log"
$genPdf = Join-Path $adoRoot "tools\scripts\gen_nav_bench_pdf.py"
$seedSql = Join-Path $adoRoot "tools\scripts\seed_nav_clientes_pg.sql"
$py = Get-OpenAdsPython

if (-not $PgUri) { $PgUri = $env:OPENADS_TEST_PG_URI }
if (-not $PgUri) { $PgUri = "postgresql://postgres@127.0.0.1:5433/postgres" }
if ($PgPort -le 0) {
    if ($PgUri -match ':(\d+)/') { $PgPort = [int]$Matches[1] } else { $PgPort = 5433 }
}

function Test-PgPort {
    param([int]$Port)
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $c.Connect("127.0.0.1", $Port)
        $c.Close()
        return $true
    } catch { return $false }
}

$oadll = Resolve-OpenAdsDllDir -Backend postgresql -Override $PgSrc

$pgBin = $env:OPENADS_PSQL_BIN
if (-not $pgBin) {
    $psqlCmd = Get-Command psql -ErrorAction SilentlyContinue
    if ($psqlCmd) { $pgBin = Split-Path -Parent $psqlCmd.Source }
}
if (-not $pgBin -or -not (Test-Path (Join-Path $pgBin "psql.exe"))) {
    Write-Error "psql not found. Set OPENADS_PSQL_BIN or add psql to PATH"
    exit 1
}
if (-not (Test-Path $seedSql)) {
    Write-Error "seed SQL missing: $seedSql"
    exit 1
}
if (-not (Test-PgPort $PgPort)) {
    Write-Error "PostgreSQL not listening on port $PgPort. Start the server and set OPENADS_TEST_PG_URI"
    exit 1
}

Write-Host "[pg] seed fixture clientes..."
& (Join-Path $pgBin "psql.exe") -h 127.0.0.1 -p $PgPort -U postgres -d postgres -v ON_ERROR_STOP=1 -f $seedSql 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$env:OPENADS_TEST_PG_URI = $PgUri
$env:OPENADS_NAV_MODE = "postgresql"
$env:OPENADS_NAV_BENCH_ITERS = "$Iters"
$env:OPENADS_NAV_BENCH_WARMUP = "$Warmup"
$env:PATH = "$oadll;$pgBin;$env:PATH"

$run = Invoke-NavBenchDemo -DemoDir $demo -Mode pg -Iters $Iters -Warmup $Warmup -OaDll $oadll -LogPath $logOut
$stdout = Get-Content $logOut -Raw -ErrorAction SilentlyContinue
if (-not $stdout) { $stdout = "" }

Write-NavBenchJson -OutPath $jsonOut -Payload @{
    timestamp = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    branch    = (Get-OpenAdsGitBranch $adoRoot)
    mode      = "postgresql"
    iters     = $Iters
    warmup    = $Warmup
    exit_code = $run.ExitCode
    wall_ms   = $run.WallMs
    pg_uri    = $PgUri
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