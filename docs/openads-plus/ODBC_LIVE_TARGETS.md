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

- **Microsoft Access** — 5 cases / 83 assertions (CI fixture).
- **SQL Server 2022** — 5 cases / 83 assertions, via the *ODBC Driver 18 for
  SQL Server*. The unmodified backend (same binary that passes against
  Access) navigates a `dbo.clientes` table by primary key with no
  dialect-specific changes: `SQLPrimaryKeys` is honoured, the identifier
  quote character is discovered via `SQLGetInfo`, and numeric literals are
  emitted type-aware.

Both read navigation (`GO TOP` / `SKIP` / `SEEK`) and navigational write
(`AdsAppendRecord` → `AdsSetString`/`AdsSetDouble` → `AdsWriteRecord`,
plus `AdsDeleteRecord`) are exercised against the same fixture on both
drivers. Write stages field values and flushes one `INSERT` (append) or
`UPDATE` (positioned edit) per record; `AdsDeleteRecord` issues a `DELETE`
by primary key. v1 expects the caller to supply the primary key on append
(no IDENTITY round-trip yet) and emits SQL literals (parameter binding is a
later hardening slice).

When `OPENADS_TEST_ODBC_CONNSTR` is unset, the live cases skip (the backend
is still exercised by the URI-parsing unit tests), so the suite stays green
on machines without a configured data source.

## Driver compatibility matrix (verified 2026-06-23)

Suite binary: `build/odbc-verify/tests/openads_unit_tests.exe` (`--test-case=*odbc*`)  
Harness: `tools/scripts/run_odbc_tests.ps1` (Access) · `run_odbc_tests_live.ps1 -ConnStr ...` (others)  
Total test cases in filter: 10 (4 unit-only always run; 6 live, skip when `OPENADS_TEST_ODBC_CONNSTR` unset)

| Driver | PK discovery | Quote char | Read/nav | Seek | Write | NULL/empty | Composite+date+decimal |
|--------|:------------:|:----------:|:--------:|:----:|:-----:|:----------:|:----------------------:|
| Microsoft Access Driver (*.mdb, *.accdb) | SQLStatistics | `` ` `` | ✅ | ✅ | ✅ | ⚠️ | ⚠️ |
| ODBC Driver 18 for SQL Server | SQLPrimaryKeys | `"` | ✅ | ✅ | ✅ | ✅ | ✅ |
| Firebird ODBC Driver | SQLPrimaryKeys | `"` | ✅ | ✅ | ✅ | ✅ | ✅ |
| PostgreSQL Unicode | — | — | — | — | — | — | — |
| MariaDB ODBC | — | — | — | — | — | — | — |

Legend: ✅ pass · ⚠️ pass-with-note (see below) · ❌ fail · — not verified (driver absent or not tested)

**Assertion counts per run:**

| Driver | Total assertions | Of which live | Pedidos seeded |
|--------|:----------------:|:-------------:|:--------------:|
| Microsoft Access | 134 / 134 | 114 | No (ADOX composite-PK skip) |
| SQL Server (LocalDB DEVAI, v16) | 145 / 145 | 125 | Yes |
| Firebird 4.0 (portable embedded+server) | 145 / 145 | 125 | Yes |

**Notes on ⚠️ cells:**

1. **Access — NULL/empty (⚠️):** The backend correctly binds an empty numeric value as SQL `NULL`
   via `SQLBindParameter` with `SQL_NULL_DATA`. Access/Jet coerces empty strings to `NULL` on
   `DOUBLE` columns regardless of how the driver submits them, so the test passes but does not
   distinguish whether the fix (bound-parameter `NULL` vs literal `NULL`) is exercised — the
   column type coercion masks both paths. SQL Server, which enforces strict typing, proves the
   binding fix conclusively.

2. **Access — Composite+date+decimal (⚠️):** The ADOX `Key.Append` call for the composite-PK
   `pedidos` fixture fails with `"item not found in collection"` on the installed ACE 16
   version. The harness (`run_odbc_tests.ps1`) catches this and warns; the test binary opens the
   table, finds it absent, and logs `"pedidos fixture absent; skipping"`. The test case is
   counted as passed (early-return pattern, no assertions executed). The composite-PK code path
   in the backend is exercised by SQL Server and Firebird.

3. **Access — PK discovery via SQLStatistics:** The Access/Jet ODBC driver returns an empty
   result set for `SQLPrimaryKeys` (not an error, just no rows). The backend falls through to
   `SQLStatistics` and picks up the `PRIMARY KEY` unique index created by `CONSTRAINT pk
   PRIMARY KEY` in the DDL. Navigation is fully correct; only the discovery route differs.

**Absent drivers:**

- **PostgreSQL Unicode** — driver not installed on this machine; system-level installation
  requires owner authorisation. Record: not verified (driver absent).
- **MariaDB ODBC** — driver not installed on this machine; system-level installation requires
  owner authorisation. Record: not verified (driver absent).
