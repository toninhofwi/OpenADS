# ODBC backend — verified live targets

The ODBC Plus backend (`odbc://`) is data-source agnostic: it reaches any
engine through its ODBC driver using only standard catalog calls
(`SQLPrimaryKeys` / `SQLStatistics` / `SQLColumns`) and portable SQL. No
per-dialect code lives in the backend.

The same ABI test cases (`--test-case=*odbc*`) run against any target by
pointing `OPENADS_TEST_ODBC_CONNSTR` at it. Two reproducible harnesses ship:

| Target | Harness | Notes |
|--------|---------|-------|
| Microsoft Access (`.accdb`) | `tools/scripts/run_odbc_tests.ps1` | Zero-server; ADOX fixture, available out of the box on Windows. |
| SQL Server / PostgreSQL / MariaDB / Firebird | `tools/scripts/run_odbc_tests_live.ps1 -ConnStr '...'` | Connection-string driven; seeds the `clientes` fixture over ODBC. |

## Verified

- **Microsoft Access** — 4 cases / 59 assertions (CI fixture).
- **SQL Server 2022** — 4 cases / 59 assertions, via the *ODBC Driver 18 for
  SQL Server*. The unmodified backend (same binary that passes against
  Access) navigates a `dbo.clientes` table by primary key with no
  dialect-specific changes: `SQLPrimaryKeys` is honoured, the identifier
  quote character is discovered via `SQLGetInfo`, and numeric literals are
  emitted type-aware.

When `OPENADS_TEST_ODBC_CONNSTR` is unset, the live cases skip (the backend
is still exercised by the URI-parsing unit tests), so the suite stays green
on machines without a configured data source.
