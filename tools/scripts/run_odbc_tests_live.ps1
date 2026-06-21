<#
  Run the OpenADS ODBC ABI e2e tests against a live SQL data source.

  Unlike run_odbc_tests.ps1 (which spins up a zero-server Microsoft Access
  fixture via ADOX), this script targets any server reachable through an
  ODBC driver -- SQL Server, PostgreSQL, MariaDB, Firebird, ... -- given a
  connection string. It seeds the `clientes` fixture table over ODBC,
  exports OPENADS_TEST_ODBC_CONNSTR, and runs the unit-test binary filtered
  to the ODBC cases.

  Usage:
    pwsh tools/scripts/run_odbc_tests_live.ps1 -ConnStr '<odbc connection string>'

  Examples (connection strings are environment-specific):
    # SQL Server
    -ConnStr 'Driver={ODBC Driver 18 for SQL Server};Server=<host>;Database=<db>;Trusted_Connection=yes;Encrypt=no;TrustServerCertificate=yes;'
    # PostgreSQL
    -ConnStr 'Driver={PostgreSQL Unicode};Server=<host>;Port=5432;Database=<db>;Uid=<user>;Pwd=<pwd>;'
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$ConnStr,
    [string]$BuildDir = "build\odbc-msvc"
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe  = Join-Path $root (Join-Path $BuildDir "tests\openads_unit_tests.exe")
if (-not (Test-Path $exe)) {
    Write-Error "test binary not found: $exe (build first)"
    exit 1
}

# Seed the fixture table the ABI tests expect:
#   clientes(id INT PRIMARY KEY, nome VARCHAR(64), saldo FLOAT)
#   rows (1,'Ana',10.5), (2,'Bob',NULL), (3,'Cid',0.0)
# Standard SQL; portable across SQL Server / PostgreSQL / MariaDB / Firebird.
Add-Type -AssemblyName System.Data
$c = New-Object System.Data.Odbc.OdbcConnection($ConnStr)
$c.Open()
function Invoke-Sql([string]$sql, [bool]$ignoreErr = $false) {
    $cmd = $c.CreateCommand(); $cmd.CommandText = $sql
    try { [void]$cmd.ExecuteNonQuery() }
    catch { if (-not $ignoreErr) { throw } }
}
Invoke-Sql "DROP TABLE clientes" $true
Invoke-Sql "CREATE TABLE clientes (id INT NOT NULL PRIMARY KEY, nome VARCHAR(64), saldo FLOAT)"
Invoke-Sql "INSERT INTO clientes (id, nome, saldo) VALUES (1, 'Ana', 10.5)"
Invoke-Sql "INSERT INTO clientes (id, nome, saldo) VALUES (2, 'Bob', NULL)"
Invoke-Sql "INSERT INTO clientes (id, nome, saldo) VALUES (3, 'Cid', 0.0)"
$c.Close()

$env:OPENADS_TEST_ODBC_CONNSTR = $ConnStr
& $exe --test-case=*odbc*
exit $LASTEXITCODE
