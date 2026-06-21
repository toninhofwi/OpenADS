<#
  NAV bench timed (Harbour) + JSON + PDF — SQLite backend (portable).
  Usage: pwsh tools/scripts/run_nav_bench_sqlite.ps1 [-Iters 30] [-Warmup 1]
  Optional env: OPENADS_DLL_SRC, OPENADS_REPO_ROOT, OPENADS_WORKTREE_ROOT
#>
param(
    [int]$Iters = 30,
    [int]$Warmup = 1,
    [string]$SqliteSrc = "",
    [string]$DbPath = ""
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "nav_bench_common.ps1")

$adoRoot = Get-OpenAdsRepoRoot
$demo = Join-Path $adoRoot "examples\fivewin"
$fixtures = Join-Path $adoRoot "tools\bench\fixtures"
$dbFile = if ($DbPath) { $DbPath } else { Join-Path $fixtures "nav_clientes.db" }
$seedPy = Join-Path $adoRoot "tools\scripts\seed_nav_clientes_sqlite.py"
$resultsDir = Join-Path $adoRoot "tools\bench\results"
$jsonOut = Join-Path $resultsDir "nav_bench_sqlite_latest.json"
$logOut = Join-Path $resultsDir "nav_bench_sqlite_latest.log"
$genPdf = Join-Path $adoRoot "tools\scripts\gen_nav_bench_pdf.py"
$py = Get-OpenAdsPython

$oadll = Resolve-OpenAdsDllDir -Backend sqlite -Override $SqliteSrc

if (-not $py) {
    Write-Error "python not found (set OPENADS_PYTHON or install python3)"
    exit 1
}
& $py $seedPy $dbFile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$uri = Format-SqliteUri $dbFile
$env:OPENADS_TEST_SQLITE_URI = $uri
$env:OPENADS_NAV_MODE = "sqlite"
$env:OPENADS_NAV_BENCH_ITERS = "$Iters"
$env:OPENADS_NAV_BENCH_WARMUP = "$Warmup"
$env:PATH = "$oadll;$env:PATH"

$run = Invoke-NavBenchDemo -DemoDir $demo -Mode sqlite -Iters $Iters -Warmup $Warmup -OaDll $oadll -LogPath $logOut
$stdout = Get-Content $logOut -Raw -ErrorAction SilentlyContinue
if (-not $stdout) { $stdout = "" }

Write-NavBenchJson -OutPath $jsonOut -Payload @{
    timestamp  = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ss")
    branch     = (Get-OpenAdsGitBranch $adoRoot)
    mode       = "sqlite"
    iters      = $Iters
    warmup     = $Warmup
    exit_code  = $run.ExitCode
    wall_ms    = $run.WallMs
    sqlite_uri = $uri
    fixture    = (Resolve-Path $dbFile).Path
    oadll      = $oadll
    stdout     = $stdout
}

Write-Host "[bench] exit=$($run.ExitCode) wall_ms=$($run.WallMs) json=$jsonOut"
if ($run.ExitCode -ne 0) { exit $run.ExitCode }

& $py $genPdf $jsonOut
exit $LASTEXITCODE