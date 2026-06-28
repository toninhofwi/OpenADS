# OpenADS Plus — SQL backends (SQLRDD parity)

OpenADS extends the ACE ABI with navigational SQL table drivers behind URI connections. Harbour `rddads` and X# `VOAds` apps can use the same `db*` / `Ads*` API against DBF files **or** SQL engines — the pattern popularized by xHarbour **SQLRDD**.

## Connection URIs

| Backend | URI examples |
|---------|----------------|
| SQLite | `sqlite:///path/to/app.db` |
| PostgreSQL | `postgresql://user:pass@host:5432/dbname` |
| MariaDB / MySQL | `mariadb://user:pass@host:3306/dbname` |
| Microsoft SQL Server | `mssql://user:pass@host:1433/dbname` |
| Firebird | `firebird://user:pass@host:3050/dbname` |
| ODBC gateway | `odbc://DSN` or `odbc://user:pass@DSN` |

```c
AdsConnect60("sqlite://C:/data/app.db", ADS_LOCAL_SERVER, NULL, NULL, 0, &hConn);
AdsOpenTable(hConn, "customers", "customers", ADS_DEFAULT, 0, 0, 0, ADS_READONLY, &hTable);
```

## SQLRDD parity matrix (2026-06)

Capabilities below are wired through the **navigational ABI** (`AdsGotoTop`, `AdsSeek`, `AdsAppendRecord`, `AdsLockRecord`, …), not only via `AdsExecuteSQLDirect` passthrough.

| Capability | SQLite | PostgreSQL | MariaDB | MSSQL | Firebird | ODBC |
|------------|--------|------------|---------|-------|----------|------|
| Read + navigation | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `dbSeek` / column index | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Write (`dbAppend` / REPLACE / `dbDelete`) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Transactions (`AdsBeginTransaction` …) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `SET FILTER` / AOF push-down | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Aggregates push-down | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `rLock()` / `fLock()` emulation | ✅ | ✅ | ✅ | ✅ (app lock) | ✅ | ✅ |
| `AdsSetRelation` / scoped | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `AdsCreateTable` (SQL DDL) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `AdsRestructureTable` ADD | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `AdsRestructureTable` DROP/CHANGE | ✅ | ✅ | ✅ | ✅ | ✅ | ✅* |
| `AdsDropTable` (navigational DDL) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `AdsClearFilter` (SQL push-down clear) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `system.tables` / `system.columns` catalog | ✅ | ✅† | ✅† | ✅† | ✅† | ✅† |
| SQL passthrough cursor (`AdsExecuteSQLDirect` SELECT) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

\* SQLite `CHANGE` is a no-op at the SQL layer (length not enforced on TEXT). Use passthrough DDL for complex SQLite schema migrations.

† Catalog queries are rewritten to `information_schema` / `sqlite_master` when you `SELECT * FROM system.tables` (or `system.columns`, `system.iota`) on a SQL URI connection.

### Issue #103 — resolved

[GitHub #103](https://github.com/FiveTechSoft/OpenADS/issues/103) tracked write + lock parity. As of 1.5.x:

- `AdsAppendRecord` / `AdsSetString` / `AdsWriteRecord` / `AdsDeleteRecord` dispatch to all six SQL backends.
- `AdsLockRecord` / `AdsLockTable` use backend-specific lock tables or `sp_getapplock` (MSSQL).
- CI runs `sql_uri_smoke`, `sqlite_filter_pushdown`, and `sqlite_seek_smoke` on every push.

Remaining gaps vs full xHarbour SQLRDD (not #103):

- Oracle native OCI (ODBC only today).
- Live CI for MSSQL / Firebird (set `OPENADS_TEST_MSSQL_CONNSTR` locally).

`SR_MGMNT*` parity: open `system.tables` / `system.columns` / `system.iota` as navigational workareas on SQL URI connections (`AdsOpenTable` or `AdsExecuteSQLDirect`). `AdsPrepareSQL` + `AdsExecuteSQL` named parameters work on all SQL backends. CI job `sql-live-pg-maria` runs against service containers on every push.

## DDL via navigational API

```c
// CREATE — field list is rddads format (NAME,Type,Len,Dec;…)
AdsCreateTable(hConn, "items", NULL, ADS_CDX, 0, 0, 0, 0,
               "ID,AutoIncrement;NAME,Character,40", &hTable);

// ADD column
AdsRestructureTable(hConn, "items", NULL, 0, 0, 0, 0,
                    "NOTE,Character,20", NULL, NULL);

// DROP column (delete list = bare names, semicolon-separated)
AdsRestructureTable(hConn, "items", NULL, 0, 0, 0, 0,
                    NULL, "NOTE", NULL);

// CHANGE length/decimals (same type)
AdsRestructureTable(hConn, "items", NULL, 0, 0, 0, 0,
                    NULL, NULL, "NAME,Character,80");

// DROP table
AdsDropTable(hConn, "items", 0);
```

Schema introspection on SQL URI connections:

```c
AdsCreateSQLStatement(hConn, &hStmt);
AdsExecuteSQLDirect(hStmt, "SELECT * FROM system.tables", &hCur);
AdsExecuteSQLDirect(hStmt, "SELECT * FROM system.columns", &hCur);
```

Passthrough DDL/DML also works:

```c
AdsCreateSQLStatement(hConn, &hStmt);
AdsExecuteSQLDirect(hStmt, "CREATE TABLE t(id INTEGER PRIMARY KEY)", &hCur);
```

## Build (Windows)

```bat
cmake --preset msvc-x64
cmake --build build/msvc-x64 --config Release --target openads_ace
```

Copy `build\msvc-x64\src\Release\openace64.dll` as `ace64.dll` next to your Harbour / X# executable.

## Tests

```bat
cmake --build build\default --config Release --target openads_unit_tests
ctest -C Release -R "sql_uri|sqlite_"
```

PostgreSQL / MariaDB / MSSQL live tests skip unless `OPENADS_TEST_PG_URI`, `OPENADS_TEST_MARIADB_URI`, or `OPENADS_TEST_MSSQL_CONNSTR` is set.

## Security

- Table/column identifiers: ASCII `[A-Za-z0-9_]` only (`is_safe_identifier`).
- Filter / AOF SQL fragments: produced by `try_emit_sql_where` (trusted subset).
- Connection URIs: assemble at runtime; never hardcode credentials in source.