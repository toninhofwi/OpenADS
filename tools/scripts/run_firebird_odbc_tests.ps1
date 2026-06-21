<#
  Run OpenADS ODBC ABI tests against the portable Firebird .fdb fixture.

  Prereqs:
    - Firebird ODBC driver installed (see _UtlAI\firebird\install_firebird_odbc.bat)
    - gds32.dll on PATH via FIREBIRD home (script sets this)
    - devai_test.fdb seeded with table clientes (see seed_odbc_clientes.sql)

  Usage:
    pwsh tools/scripts/run_firebird_odbc_tests.ps1 [-BuildDir build\odbc-msvc]
#>
param(
    [string]$BuildDir = "build\odbc-msvc"
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe  = Join-Path $root (Join-Path $BuildDir "tests\openads_unit_tests.exe")
if (-not (Test-Path $exe)) {
    Write-Error "test binary not found: $exe (build with OPENADS_WITH_ODBC=ON first)"
    exit 1
}

# DEVAI root = parent of _Prj (portable SSD — no drive hardcode in logic).
$devaiRoot = (Resolve-Path (Join-Path $root "..\..")).Path
$fbHome    = Join-Path $devaiRoot "_UtlAI\firebird"
$fbDb      = Join-Path $devaiRoot "_Prj\data\firebird\devai_test.fdb"
$fbPass    = "devai_fb"
$cfg       = Join-Path $devaiRoot "config_ai\.firebird\credenciais.txt"

if (Test-Path $cfg) {
    Get-Content $cfg | ForEach-Object {
        if ($_ -match '^\s*SYSDBA_PASSWORD\s*=\s*(.+)\s*$') { $fbPass = $Matches[1].Trim() }
    }
}

if (-not (Test-Path $fbHome)) {
    Write-Error "Firebird portable not found: $fbHome (run setup_firebird.bat)"
    exit 1
}
if (-not (Test-Path $fbDb)) {
    Write-Error "test database not found: $fbDb (run create_test_db.bat + seed_odbc_clientes.sql)"
    exit 1
}

$drivers = Get-OdbcDriver -Platform "64-bit" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'Firebird' }
if (-not $drivers) {
    Write-Error "Firebird ODBC driver not installed — run install_firebird_odbc.bat"
    exit 1
}

# fbclient as gds32 for the ODBC driver (no copy into System32 required).
$gds = Join-Path $fbHome "gds32.dll"
if (-not (Test-Path $gds)) {
    Copy-Item (Join-Path $fbHome "fbclient.dll") $gds -Force
}

$env:FIREBIRD = $fbHome
$env:PATH     = "$fbHome;$env:PATH"

$connstr = "Driver={Firebird ODBC Driver};Database=$fbDb;Uid=SYSDBA;Pwd=$fbPass;Charset=UTF8;"
$env:OPENADS_TEST_ODBC_CONNSTR = $connstr

Write-Host "[openads] Firebird ODBC fixture: $fbDb"
& $exe --test-case=*odbc*
exit $LASTEXITCODE