# ODBC backend hardening — design

Date: 2026-06-22
Status: approved (pre-implementation)
Scope: harden the generic ODBC backend (`src/sql_backend/odbc_*`) behind the ACE ABI.

## Problem

The ODBC backend currently builds every value-carrying SQL statement by
string concatenation (`escape_literal` / `format_literal` /
`is_numeric_literal` in `odbc_connection.cpp`). This v1 shortcut has three
concrete weaknesses:

1. **NULL-on-write bug.** An xBase numeric field written empty reaches
   `format_literal` as a numeric column with `is_numeric_literal("") == false`,
   so it is emitted as `''` (empty string literal). `INSERT ''` into a numeric
   column is rejected or coerced to garbage by most drivers. The correct value
   is SQL `NULL`.
2. **Narrow type coverage.** `map_odbc_column` collapses everything that is not
   integer/double/bit/binary into `ADS_STRING`. Date/time columns are not
   recognised and are emitted as quoted string literals, which is
   dialect-dependent. `format_literal` only distinguishes numeric vs quoted.
3. **Value-axis robustness.** Literal values are escaped by hand (only `'` →
   `''`). Drivers with different escaping rules, and any value-axis injection
   surface, are handled only by that manual escape.

The canonical fix for all three at once is **parameter binding**
(`SQLBindParameter` + `?` placeholders): the driver performs text→type
coercion, `NULL` is carried by the indicator, and the value axis stops being a
string-concatenation surface. Identifiers (table/column names) cannot be bound;
they remain validated (`is_safe_identifier`) and quoted (`quote_ident`).

## Goals

- Replace all value-carrying literal SQL (WHERE/seek/INSERT/UPDATE/DELETE) with
  bound parameters.
- Fix NULL-on-write and add date/time/logical/decimal correctness.
- Add focused tests: NULL round-trip, empty-numeric→NULL, unicode, date column
  read+seek, decimal precision, multi-column PK nav+seek, value-axis injection.
- Verify the backend live against PostgreSQL / MariaDB / Firebird over ODBC (in
  addition to the already-verified Access and SQL Server) and record a driver
  compatibility matrix.

## Non-goals (deliberately deferred)

- Seek `O(log N)` via binary search over the PK snapshot (today linear scan).
- Filter → `WHERE` pushdown.
- Connection reconnect/robustness.
- Binary (`SQL_BINARY`/`VARBINARY`) write through `SQL_C_CHAR` — remains a
  documented v1 limitation (rare in legacy xBase navigation).

## Design

### 1. Retain raw SQL type; recognise dates

`OdbcTable::FieldDesc` gains two raw fields captured from `SQLColumns`:

```cpp
struct FieldDesc {
    std::string   name;
    std::uint16_t type        = 0;   // mapped ADS type (existing)
    std::uint32_t length      = 0;
    std::uint16_t decimals    = 0;
    bool          nullable    = true;
    int           sql_type    = 0;   // NEW: raw ODBC SQL_* type code
    std::uint32_t column_size = 0;   // NEW: raw COLUMN_SIZE
};
```

`map_odbc_column` records `fd.sql_type = sql_type` and `fd.column_size`, and
recognises date/time types — `SQL_TYPE_DATE`/`TIME`/`TIMESTAMP` (91/92/93) and
the legacy `DATE`/`TIME`/`TIMESTAMP` (9/10/11) — mapping them to `ADS_DATE`
while retaining the raw code. (All other mappings are unchanged.)

### 2. Bound-parameter plumbing

A small value type plus a parameterised `run_query`:

```cpp
struct BoundParam {
    std::string  value;       // text form; storage must outlive SQLExecDirect
    bool         is_null  = false;
    SQLSMALLINT  sql_type = SQL_VARCHAR;  // ParameterType
    SQLULEN      col_size = 0;            // ColumnSize
    SQLSMALLINT  decimals = 0;            // DecimalDigits
};

util::Result<void> run_query(
    SQLHDBC dbc, const std::string& sql,
    const std::vector<BoundParam>& params,
    std::vector<std::vector<std::string>>& rows,
    std::vector<std::vector<bool>>& nulls,
    SQLULEN max_rows = 0);
```

`run_query` allocates a statement, binds each param with
`SQLBindParameter(st, i+1, SQL_PARAM_INPUT, SQL_C_CHAR, p.sql_type, p.col_size,
p.decimals, buf, len, &ind[i])` — the value buffer points into the stable
`params` vector; a parallel `std::vector<SQLLEN>` holds the indicators
(`SQL_NULL_DATA` when `is_null`, else the byte length) — then `SQLExecDirect`.
The existing no-param call sites pass an empty `params` vector (metadata reads:
`discover_pk`, `describe_columns`, `load_pk_snapshot` ORDER BY — none carry
values).

`ParameterType` is taken from the target column's retained `sql_type`
(`col_size`/`decimals` alongside). If a column's `sql_type` is unknown (0),
default to `SQL_VARCHAR`. For an `UpperColumn` seek expression the comparison is
textual, so the key binds as `SQL_VARCHAR`.

### 3. Builders emit placeholders

The SQL builders take a `std::vector<BoundParam>&` out-param and emit `?`:

- `pk_where_clause` → `col = ? AND col = ?`, pushing each PK value with its
  column type. Used by `load_current_row`, the UPDATE in `flush_table`, and
  `delete_record`.
- `seek_index` → `WHERE expr >= ?` / `= ?` / `<= ?`, pushing the key with the
  seek column's type (or `SQL_VARCHAR` for `UpperColumn`).
- `flush_table` INSERT → `VALUES (?, ?, …)` pushing each staged value.
- `flush_table` UPDATE → `SET col = ?, …` pushing each staged value.

`escape_literal`, `is_numeric_literal`, and `format_literal` are removed.
`quote_ident` and `index_column_sql` (the `UPPER(...)` wrapper) stay.

### 4. NULL-on-write rule

When building a param for a staged write value: if the value string is empty
**and** the column is non-textual (numeric / double / logical / date / binary),
bind SQL `NULL` (`is_null = true`); if the column is textual, bind the empty
string. This eliminates the `''`-into-numeric bug. A non-empty numeric value
(e.g. `"0"`) binds normally. Rule is documented in code and covered by a test.

### 5. Tests

New cases in the unit/e2e suite, each driver-agnostic where the fixture allows
and gated by connstr env for live targets:

- NULL round-trip: write NULL into a nullable numeric, read back `is_null`.
- Empty-numeric → NULL (the §4 fix).
- Unicode value round-trip (UTF-8 text).
- Date column read + seek.
- Decimal precision round-trip.
- Multi-column PK navigation + seek.
- Value-axis injection: `O'Brien`, `'); DROP TABLE x; --` stored literally,
  read back intact, no statement breakage.

### 6. Driver matrix

Extend `tools/scripts/run_odbc_tests_live.ps1` (already connstr-driven) and
`docs/openads-plus/ODBC_LIVE_TARGETS.md` with PostgreSQL, MariaDB, and Firebird
ODBC connection strings, and record a compatibility matrix (PK discovery path,
quote char, type coercion, write round-trip per driver). Honest-coverage rule:
if a required ODBC driver is not installed, the matrix records "not verified"
for that target rather than asserting success.

## Verification

- Unit suite stays green (528+), plus the new cases.
- x64 build via the pinned recipe (MSVC x64 env + winlibs cmake/ninja,
  `OPENADS_WITH_ODBC=ON`).
- Live E2E: Access (CI fixture) + SQL Server, plus whatever the driver matrix
  reaches.

## Risks

- PostgreSQL / MariaDB ODBC drivers may not be installed; installing them is a
  non-portable system change requiring explicit authorization. Without it, the
  matrix entries are recorded as "not verified" (no phantom success).
- PK snapshot reconciliation after write still compares PK values as text;
  numeric-format divergence between staged and read-back values is a
  pre-existing limitation, unchanged by binding. Noted, not addressed here.
