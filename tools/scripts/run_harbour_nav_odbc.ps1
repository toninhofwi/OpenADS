<#
  Harbour NAV smoke against the same Firebird ODBC fixture as run_firebird_odbc_tests.ps1.
#>
param(
    [string]$OdbcSrc = "OpenADS-odbc\build\odbc-msvc\src",
    [int]$Repeats = 1
)
$ErrorActionPreference = "Stop"

$adoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$prjRoot = Split-Path -Parent $adoRoot
$demo = Join-Path $adoRoot "examples\fivewin"
$oadll = (Resolve-Path (Join-Path $prjRoot $OdbcSrc)).Path
# Firebird fixture config via env (no hardcoded paths/secrets); fall back to a local layout.
$fbHome = if ($env:OPENADS_FIREBIRD_HOME) { $env:OPENADS_FIREBIRD_HOME } else { Join-Path $prjRoot "firebird" }
$fbDb   = if ($env:OPENADS_FIREBIRD_DB)   { $env:OPENADS_FIREBIRD_DB }   else { Join-Path $prjRoot "data\firebird\nav_test.fdb" }
$fbPass = if ($env:OPENADS_FIREBIRD_PASS) { $env:OPENADS_FIREBIRD_PASS } else { "masterkey" }

$exe = Join-Path $demo "demo_nav_multidb.exe"
if (-not (Test-Path $exe)) {
    Write-Error "build first: examples\fivewin\demo_nav_run.bat odbc"
    exit 1
}

$driverName = if ($env:OPENADS_FIREBIRD_DRIVER) { $env:OPENADS_FIREBIRD_DRIVER } else { "Firebird ODBC Driver" }
$drivers = Get-OdbcDriver -Platform "64-bit" -ErrorAction SilentlyContinue
if ($drivers.Name -notcontains $driverName) {
    if ($drivers.Name -contains "Firebird ODBC Driver") {
        $driverName = "Firebird ODBC Driver"
    } else {
        Write-Error "Firebird ODBC driver not registered"
        exit 1
    }
}

$gds = Join-Path $fbHome "gds32.dll"
if (-not (Test-Path $gds)) {
    Copy-Item (Join-Path $fbHome "fbclient.dll") $gds -Force
}

$connstr = "Driver={$driverName};Database=$fbDb;Uid=SYSDBA;Pwd=$fbPass;Charset=UTF8;"
$env:OPENADS_TEST_ODBC_CONNSTR = $connstr
$env:OPENADS_NAV_MODE = "odbc"
$env:FIREBIRD = $fbHome
$env:PATH = "$oadll;$fbHome;$env:PATH"

Copy-Item (Join-Path $oadll "openace64.dll") (Join-Path $demo "openace64.dll") -Force

Push-Location $demo
try {
    for ($i = 1; $i -le $Repeats; $i++) {
        if ($Repeats -gt 1) { Write-Host "--- run $i / $Repeats ---" }
        & $exe
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
} finally {
    Pop-Location
}