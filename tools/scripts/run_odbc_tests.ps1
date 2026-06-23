<#
  Run the OpenADS ODBC ABI e2e tests against a throwaway fixture.

  Creates a temporary Microsoft Access .accdb (via the ACE OLE DB
  provider that ships with the Access Database Engine / Office), seeds
  the `clientes` table, exports OPENADS_TEST_ODBC_CONNSTR, and runs the
  unit-test binary filtered to the ODBC cases. Any data source reachable
  through an ODBC driver works the same way; Access is just a zero-server
  fixture available out of the box on Windows.

  Usage:
    pwsh tools/scripts/run_odbc_tests.ps1 [-BuildDir build\odbc-msvc]
#>
param(
    [string]$BuildDir = "build\odbc-msvc"
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe  = Join-Path $root (Join-Path $BuildDir "tests\openads_unit_tests.exe")
if (-not (Test-Path $exe)) {
    Write-Error "test binary not found: $exe (build first)"
    exit 1
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("openads_odbc_" + [System.Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $work | Out-Null
$accdb = Join-Path $work "fixture.accdb"

$cat = New-Object -ComObject ADOX.Catalog
$cat.Create("Provider=Microsoft.ACE.OLEDB.16.0;Data Source=$accdb;") | Out-Null

$connstr = "Driver={Microsoft Access Driver (*.mdb, *.accdb)};DBQ=$accdb;"
$c = New-Object System.Data.Odbc.OdbcConnection($connstr)
$c.Open()
foreach ($sql in @(
    "CREATE TABLE clientes (id INTEGER CONSTRAINT pk PRIMARY KEY, nome VARCHAR(64), saldo DOUBLE)",
    "INSERT INTO clientes (id,nome,saldo) VALUES (1,'Ana',10.5)",
    "INSERT INTO clientes (id,nome,saldo) VALUES (2,'Bob',NULL)",
    "INSERT INTO clientes (id,nome,saldo) VALUES (3,'Cid',0.0)")) {
    $cmd = $c.CreateCommand(); $cmd.CommandText = $sql; [void]$cmd.ExecuteNonQuery()
}
$c.Close()

$env:OPENADS_TEST_ODBC_CONNSTR = $connstr
& $exe --test-case=*odbc*
exit $LASTEXITCODE
