<#
  Run the OpenADS native Firebird backend tests against an embedded `.fdb`.

  The native backend opens the database in-process via libfbclient (no
  server, no authentication), so the whole fixture is built and queried
  embedded — nothing is installed and no service is started.

  Env:
    OPENADS_FB_CLIENT_HOME   portable Firebird install providing fbclient.dll,
                             isql.exe, the engine plugin and firebird.conf.
                             FIREBIRD is accepted as a fallback.

  Usage:
    pwsh tools/scripts/run_firebird_native_tests.ps1 [-BuildDir build\fb2]
#>
param(
    [string]$BuildDir = "build\fb2"
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe  = Join-Path $root (Join-Path $BuildDir "tests\openads_unit_tests.exe")
if (-not (Test-Path $exe)) {
    Write-Error "test binary not found: $exe (build with OPENADS_WITH_FIREBIRD=ON first)"
    exit 1
}

$clientHome = $env:OPENADS_FB_CLIENT_HOME
if (-not $clientHome -and $env:FIREBIRD) { $clientHome = $env:FIREBIRD }
if (-not $clientHome -or -not (Test-Path (Join-Path $clientHome "isql.exe"))) {
    Write-Error "Set OPENADS_FB_CLIENT_HOME (or FIREBIRD) to a portable Firebird install (needs isql.exe + fbclient.dll)."
    exit 1
}

# Embedded engine discovery: FIREBIRD points at the install (plugins +
# firebird.conf + firebird.msg); PATH resolves fbclient.dll and its deps.
$env:FIREBIRD = $clientHome
$env:PATH = "$clientHome;$env:PATH"

# Build a throwaway fixture entirely in embedded mode (no server, no auth).
$fixtureDir = Join-Path $root "tests\fixtures\firebird"
New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
$work = Join-Path $fixtureDir "native_work.fdb"
Remove-Item $work -Force -ErrorAction SilentlyContinue
$workFwd = $work -replace '\\', '/'

$seedSql = @"
CREATE DATABASE '$workFwd' USER 'SYSDBA' DEFAULT CHARACTER SET UTF8;
CREATE TABLE clientes (id INTEGER NOT NULL PRIMARY KEY, nome VARCHAR(64), saldo DOUBLE PRECISION);
INSERT INTO clientes (id, nome, saldo) VALUES (1, 'Ana', 10.5);
INSERT INTO clientes (id, nome, saldo) VALUES (2, 'Bob', NULL);
INSERT INTO clientes (id, nome, saldo) VALUES (3, 'Cid', 0.0);
COMMIT;
EXIT;
"@
$seedFile = Join-Path $fixtureDir "_native_seed.sql"
Set-Content -Path $seedFile -Value $seedSql -Encoding ASCII
try {
    & (Join-Path $clientHome "isql.exe") -user SYSDBA -i $seedFile | Out-Null
} finally {
    Remove-Item $seedFile -Force -ErrorAction SilentlyContinue
}
if (-not (Test-Path $work)) {
    Write-Error "could not create embedded fixture $work"
    exit 1
}

$env:OPENADS_TEST_FIREBIRD_DB = $work
Write-Host "[openads] native Firebird embedded fixture:" $work
& $exe --test-case=*firebird*
exit $LASTEXITCODE
