<#
  Run the OpenADS native Firebird backend tests against a Firebird database
  served over TCP (the server form of the URI: firebird://USER:PASS@host:port/db).

  Unlike run_firebird_native_tests.ps1 (which opens an embedded .fdb in-process,
  no server), this harness launches the portable Firebird engine in application
  mode (firebird.exe -a) — a normal user process, no Windows service installed —
  on a TCP port, creates and seeds a throwaway fixture over that wire, points the
  test at the server URI, then stops the process it started (by PID) and removes
  the fixture.

  Env:
    OPENADS_FB_CLIENT_HOME      portable Firebird install providing firebird.exe,
                                isql.exe, fbclient.dll, the engine plugin and
                                firebird.conf / security database. FIREBIRD is
                                accepted as a fallback.
    OPENADS_FB_SYSDBA_PASSWORD  SYSDBA password for the server's security
                                database (default: masterkey).

  Usage:
    pwsh tools/scripts/run_firebird_server_tests.ps1 [-BuildDir build\fb2] [-Port 3050]
#>
param(
    [string]$BuildDir = "build\fb2",
    [int]$Port        = 3050
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
$serverExe = if ($clientHome) { Join-Path $clientHome "firebird.exe" } else { $null }
$isqlExe   = if ($clientHome) { Join-Path $clientHome "isql.exe" }     else { $null }
if (-not $clientHome -or -not (Test-Path $serverExe) -or -not (Test-Path $isqlExe)) {
    Write-Error "Set OPENADS_FB_CLIENT_HOME (or FIREBIRD) to a portable Firebird install (needs firebird.exe + isql.exe + fbclient.dll)."
    exit 1
}

$pass = $env:OPENADS_FB_SYSDBA_PASSWORD
if (-not $pass) { $pass = "masterkey" }

# The server engine discovers plugins + firebird.conf + the security database
# via FIREBIRD; PATH resolves fbclient.dll and its deps.
$env:FIREBIRD = $clientHome
$env:PATH     = "$clientHome;$env:PATH"

$fixtureDir = Join-Path $root "tests\fixtures\firebird"
New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
$work = Join-Path $fixtureDir "server_work.fdb"
Remove-Item $work -Force -ErrorAction SilentlyContinue

$server = $null
try {
    # Refuse to hijack a server someone else is already running on this port:
    # only manage the process we spawn ourselves.
    $busy = Test-NetConnection -ComputerName 127.0.0.1 -Port $Port -WarningAction SilentlyContinue
    if ($busy.TcpTestSucceeded) {
        Write-Error "port $Port already has a listener; pass -Port <free port> or stop the other server."
        exit 1
    }

    Write-Host "[openads] starting portable Firebird (firebird.exe -a) on port $Port ..."
    $server = Start-Process -FilePath $serverExe -ArgumentList "-a" -PassThru -WindowStyle Minimized

    # Wait for the listener to accept.
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        $t = Test-NetConnection -ComputerName 127.0.0.1 -Port $Port -WarningAction SilentlyContinue
        if ($t.TcpTestSucceeded) { $ready = $true; break }
    }
    if (-not $ready) { Write-Error "Firebird server did not start listening on port $Port"; exit 1 }

    $workFwd = $work -replace '\\', '/'
    $conn    = "localhost/$Port`:$workFwd"
    $seedSql = @"
CREATE DATABASE 'localhost/$Port`:$workFwd' USER 'SYSDBA' PASSWORD '$pass' DEFAULT CHARACTER SET UTF8;
CREATE TABLE clientes (id INTEGER NOT NULL PRIMARY KEY, nome VARCHAR(64), saldo DOUBLE PRECISION);
INSERT INTO clientes (id, nome, saldo) VALUES (1, 'Ana', 10.5);
INSERT INTO clientes (id, nome, saldo) VALUES (2, 'Bob', NULL);
INSERT INTO clientes (id, nome, saldo) VALUES (3, 'Cid', 0.0);
COMMIT;
EXIT;
"@
    $seedFile = Join-Path $fixtureDir "_server_seed.sql"
    Set-Content -Path $seedFile -Value $seedSql -Encoding ASCII
    try {
        & $isqlExe -user SYSDBA -password $pass -i $seedFile | Out-Null
    } finally {
        Remove-Item $seedFile -Force -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path $work)) { Write-Error "could not create server fixture $work"; exit 1 }

    # The native backend parses firebird://USER:PASS@host:port/dbpath. The db
    # path keeps native backslashes (the parser only treats '/' / '@' as
    # delimiters, so the first '/' after the port is the authority/path split).
    $uri = "firebird://SYSDBA:$pass@localhost:$Port/$work"
    $env:OPENADS_TEST_FIREBIRD_SERVER = $uri
    Write-Host "[openads] server fixture ready:" $uri
    & $exe --test-case=*firebird*server*
    $code = $LASTEXITCODE
} finally {
    if ($server -and -not $server.HasExited) {
        Write-Host "[openads] stopping Firebird server (PID $($server.Id)) ..."
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $work -Force -ErrorAction SilentlyContinue
}
exit $code
