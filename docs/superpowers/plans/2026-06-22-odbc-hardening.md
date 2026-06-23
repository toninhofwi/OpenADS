# ODBC Backend Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace value-axis literal SQL in the ODBC backend with bound parameters, fix NULL/type correctness, and verify the backend live across multiple ODBC drivers.

**Architecture:** The generic ODBC backend (`src/sql_backend/odbc_*`) navigates a table by a primary-key snapshot and builds SQL by string concatenation. This plan threads a `BoundParam` list through a parameterised `run_query`, converts every value-carrying statement (read WHERE, seek, INSERT/UPDATE/DELETE) to `?` placeholders bound with `SQLBindParameter`, and adds a NULL-on-write rule. Identifiers stay validated and quoted. Tests are mostly live (gated by `OPENADS_TEST_ODBC_CONNSTR`); one pure unit test covers the type mapper.

**Tech Stack:** C++17, ODBC (sql.h/sqlext.h), doctest, CMake (MSVC x64 + winlibs), PowerShell seed/run scripts.

## Global Constraints

- Source-clean public repo: NO internal paths (e.g. drive-letter paths), NO third-party product names, NO business/strategy text in any committed file. Technical language only.
- Commit trailer: end every commit with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` and NOTHING else (match the branch's existing style — no session URL).
- Branch: `pr/openads-plus-odbc` (work directly on it; push each commit).
- Identifiers are never bound — they stay `is_safe_identifier`-validated and `quote_ident`-quoted. Only values bind.
- Build (x64, from the repo root):
  ```
  cmd /c 'call H:\DEVAI\_UtlAI\msvc\msvc_x64_full.bat && cd /d H:\DEVAI\_Prj\OpenADS-odbc && H:\DEVAI\_UtlAI\Mingw\bin\cmake.exe -S . -B build\odbc-verify -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DOPENADS_WITH_ODBC=ON -DOPENADS_WITH_HTTP=OFF -DOPENADS_WITH_TLS=OFF -DOPENADS_WARNINGS_AS_ERRORS=OFF -DCMAKE_TLS_VERIFY=0 && H:\DEVAI\_UtlAI\Mingw\bin\cmake.exe --build build\odbc-verify'
  ```
- Run pure unit tests (no driver): `build\odbc-verify\tests\openads_unit_tests.exe --test-case=*odbc*`
- Run live tests against a target: `pwsh tools\scripts\run_odbc_tests_live.ps1 -ConnStr '<odbc connstr>' -BuildDir build\odbc-verify`
- Access CI fixture (zero-server) seeds + runs via: `pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify`
- Fixture `clientes(id INT PK, nome VARCHAR(64), saldo FLOAT)` rows: `(1,'Ana',10.5),(2,'Bob',NULL),(3,'Cid',0.0)`. Live tests must be self-restoring (end back at the seeded state).
- ABI type codes (`include/openads/ace.h`): `ADS_LOGICAL=1`, `ADS_DATE=3`, `ADS_STRING=4`, `ADS_INTEGER=11`, `ADS_DOUBLE` (numeric), `ADS_BINARY`.

---

## File Structure

- `src/sql_backend/odbc_table.h` — `OdbcTable::FieldDesc` gains `sql_type` + `column_size`.
- `src/sql_backend/odbc_backend.cpp` — `map_odbc_column` retains raw type, recognises dates.
- `src/sql_backend/odbc_connection.cpp` — `BoundParam`, parameterised `run_query`, placeholder builders, NULL rule; remove `escape_literal`/`is_numeric_literal`/`format_literal`.
- `tests/unit/odbc_map_column_test.cpp` — NEW pure unit test for the type mapper.
- `tests/unit/abi_plus_odbc_bind_test.cpp` — NEW live tests: NULL write, empty→NULL, unicode, injection.
- `tests/unit/abi_plus_odbc_extended_test.cpp` — NEW live tests: composite PK, date, decimal (table `pedidos`).
- `tools/scripts/run_odbc_tests_live.ps1`, `tools/scripts/run_odbc_tests.ps1` — seed `pedidos`.
- `docs/openads-plus/ODBC_LIVE_TARGETS.md` — driver compatibility matrix.
- `src/CMakeLists.txt` (or the test target's source list) — add the 3 new test files.

---

### Task 1: Retain raw SQL type and recognise dates

**Files:**
- Modify: `src/sql_backend/odbc_table.h` (FieldDesc struct, lines 24-30)
- Modify: `src/sql_backend/odbc_backend.cpp` (`map_odbc_column`, lines 32-85)
- Test: `tests/unit/odbc_map_column_test.cpp` (create)
- Modify: test target source list to compile the new test (see CMake note at end of task)

**Interfaces:**
- Produces: `OdbcTable::FieldDesc{ ...; int sql_type; std::uint32_t column_size; }`. `map_odbc_column` now sets both, and maps SQL date/time types to `ADS_DATE`. Consumed by Tasks 2-4 for `BoundParam.sql_type`/`col_size`.

- [ ] **Step 1: Write the failing pure unit test**

Create `tests/unit/odbc_map_column_test.cpp`:
```cpp
#include "doctest.h"
#include "openads/ace.h"

#if defined(OPENADS_WITH_ODBC)

#include "sql_backend/odbc_backend.h"

using openads::sql_backend::map_odbc_column;

TEST_CASE("odbc map_column retains raw sql type and size") {
    // SQL_INTEGER = 4
    auto i = map_odbc_column("id", 4, false, 10, 0);
    CHECK(i.type == ADS_INTEGER);
    CHECK(i.sql_type == 4);
    CHECK(i.column_size == 10u);

    // SQL_VARCHAR = 12
    auto v = map_odbc_column("nome", 12, true, 64, 0);
    CHECK(v.type == ADS_STRING);
    CHECK(v.sql_type == 12);
    CHECK(v.column_size == 64u);
}

TEST_CASE("odbc map_column recognises date/time as ADS_DATE") {
    // SQL_TYPE_DATE = 91, SQL_TYPE_TIME = 92, SQL_TYPE_TIMESTAMP = 93
    CHECK(map_odbc_column("d", 91, true, 10, 0).type == ADS_DATE);
    CHECK(map_odbc_column("t", 92, true, 8, 0).type == ADS_DATE);
    CHECK(map_odbc_column("ts", 93, true, 23, 3).type == ADS_DATE);
    // legacy SQL_DATE = 9, SQL_TIME = 10, SQL_TIMESTAMP = 11
    CHECK(map_odbc_column("d9", 9, true, 10, 0).type == ADS_DATE);
    CHECK(map_odbc_column("ts11", 11, true, 23, 3).type == ADS_DATE);
    // raw type preserved for binding
    CHECK(map_odbc_column("ts", 93, true, 23, 3).sql_type == 93);
}

#endif
```

- [ ] **Step 2: Add the test file to the test target and run to verify it fails**

Find the unit-test target's source list (the CMake block that lists `abi_plus_odbc_read_test.cpp`):
```
grep -rn "abi_plus_odbc_read_test.cpp" src tests CMakeLists.txt
```
Add `tests/unit/odbc_map_column_test.cpp` next to it. Build (Global Constraints) and run:
```
build\odbc-verify\tests\openads_unit_tests.exe --test-case=*map_column*
```
Expected: FAIL — `FieldDesc` has no member `sql_type` (compile error), or assertions fail.

- [ ] **Step 3: Add the fields to FieldDesc**

In `src/sql_backend/odbc_table.h`, extend the struct (after `nullable`):
```cpp
    struct FieldDesc {
        std::string   name;
        std::uint16_t type        = 0;
        std::uint32_t length      = 0;
        std::uint16_t decimals    = 0;
        bool          nullable    = true;
        int           sql_type    = 0;   // raw ODBC SQL_* type code
        std::uint32_t column_size = 0;   // raw COLUMN_SIZE
    };
```

- [ ] **Step 4: Record raw type and recognise dates in map_odbc_column**

In `src/sql_backend/odbc_backend.cpp`: add date constants in the anon namespace (after `kSqlBit`):
```cpp
constexpr int kSqlDate          = 9;
constexpr int kSqlTime          = 10;
constexpr int kSqlTimestamp     = 11;
constexpr int kSqlTypeDate      = 91;
constexpr int kSqlTypeTime      = 92;
constexpr int kSqlTypeTimestamp = 93;
```
At the top of `map_odbc_column`, after `fd.nullable = nullable;`, retain raw info:
```cpp
    fd.sql_type    = sql_type;
    fd.column_size = column_size > 0
                         ? static_cast<std::uint32_t>(column_size) : 0;
```
Add a date case before the `kSqlChar`/default branch:
```cpp
        case kSqlDate:
        case kSqlTime:
        case kSqlTimestamp:
        case kSqlTypeDate:
        case kSqlTypeTime:
        case kSqlTypeTimestamp:
            fd.type     = ADS_DATE;
            fd.length   = 8;
            fd.decimals = 0;
            break;
```

- [ ] **Step 5: Run the tests to verify they pass**

```
build\odbc-verify\tests\openads_unit_tests.exe --test-case=*map_column*
```
Expected: PASS (both cases). Then run the full ODBC set to confirm no regression:
```
build\odbc-verify\tests\openads_unit_tests.exe --test-case=*odbc*
```
Expected: PASS / skipped (live cases skip without connstr).

- [ ] **Step 6: Commit**

```
git add src/sql_backend/odbc_table.h src/sql_backend/odbc_backend.cpp tests/unit/odbc_map_column_test.cpp <test-cmake-file>
git commit -m "feat(odbc): retain raw SQL type and map date/time columns to ADS_DATE

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: BoundParam + parameterised run_query; convert read/nav SQL

**Files:**
- Modify: `src/sql_backend/odbc_connection.cpp` (run_query lines 122-144; pk_where_clause 203-212; load_current_row 390-416; pk_select_list/load_pk_snapshot 194-201, 370-388)

**Interfaces:**
- Produces: `struct BoundParam{ std::string value; bool is_null=false; SQLSMALLINT sql_type=SQL_VARCHAR; SQLULEN col_size=0; SQLSMALLINT decimals=0; }`.
- Produces: `run_query(SQLHDBC, const std::string& sql, const std::vector<BoundParam>& params, rows&, nulls&, SQLULEN max_rows=0)`.
- Produces: helper `BoundParam param_for(const OdbcTable&, const std::string& column, const std::string& value)` — builds a value param using the column's retained type, applying the NULL rule (Task 4 reuses it).
- Produces: `pk_where_clause(q, tbl, pk, std::vector<BoundParam>& out)` now emits `col = ? AND ...` and appends PK params. Consumed by Tasks 3-4.

- [ ] **Step 1: Verify current live read behavior is green (baseline)**

Build (Global Constraints), then with the Access fixture:
```
pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify
```
Expected: PASS (read/seek/write cases). This is the regression baseline — read/nav behavior must be byte-identical after the refactor.

- [ ] **Step 2: Add BoundParam and the param_for helper**

In `odbc_connection.cpp`, inside the anon namespace (near `quote_ident`), add:
```cpp
struct BoundParam {
    std::string value;
    bool        is_null  = false;
    SQLSMALLINT sql_type = SQL_VARCHAR;
    SQLULEN     col_size = 0;
    SQLSMALLINT decimals = 0;
};

// True for ADS column types that must NOT receive an empty string: a blank
// xBase value on these becomes SQL NULL (see NULL-on-write rule).
bool is_textual(std::uint16_t ads_type) {
    return ads_type == ADS_STRING;
}

BoundParam param_for(const OdbcTable& tbl, const std::string& column,
                     const std::string& value) {
    BoundParam p;
    const std::size_t idx = field_index_ci(tbl, column);
    if (idx != static_cast<std::size_t>(-1)) {
        const auto& f = tbl.fields[idx];
        if (f.sql_type != 0) p.sql_type = static_cast<SQLSMALLINT>(f.sql_type);
        p.col_size = f.column_size;
        p.decimals = static_cast<SQLSMALLINT>(f.decimals);
        if (value.empty() && !is_textual(f.type)) p.is_null = true;
    }
    p.value = value;
    return p;
}
```

- [ ] **Step 3: Add a parameterised run_query overload**

Replace the body of `run_query` so it accepts params and binds them. The buffers live in the caller's `params` vector (stable); a local indicator array parallels it:
```cpp
util::Result<void> run_query(
    SQLHDBC dbc, const std::string& sql,
    const std::vector<BoundParam>& params,
    std::vector<std::vector<std::string>>& rows,
    std::vector<std::vector<bool>>& nulls,
    SQLULEN max_rows = 0) {
    SQLHSTMT st = SQL_NULL_HSTMT;
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        return odbc_error("odbc alloc stmt", odbc_diag(SQL_HANDLE_DBC, dbc));
    }
    if (max_rows > 0) {
        SQLSetStmtAttr(st, SQL_ATTR_MAX_ROWS,
                       reinterpret_cast<SQLPOINTER>(max_rows), 0);
    }
    std::vector<SQLLEN> ind(params.size());
    for (std::size_t i = 0; i < params.size(); ++i) {
        const BoundParam& p = params[i];
        ind[i] = p.is_null ? SQL_NULL_DATA
                           : static_cast<SQLLEN>(p.value.size());
        SQLRETURN br = SQLBindParameter(
            st, static_cast<SQLUSMALLINT>(i + 1), SQL_PARAM_INPUT,
            SQL_C_CHAR, p.sql_type,
            p.col_size > 0 ? p.col_size : (p.value.empty() ? 1 : p.value.size()),
            p.decimals,
            const_cast<char*>(p.value.c_str()),
            static_cast<SQLLEN>(p.value.size()), &ind[i]);
        if (!SQL_SUCCEEDED(br)) {
            auto e = odbc_error("odbc bind param",
                                odbc_diag(SQL_HANDLE_STMT, st));
            SQLFreeHandle(SQL_HANDLE_STMT, st);
            return e;
        }
    }
    SQLRETURN r = SQLExecDirect(st, sqlstr(sql), SQL_NTS);
    if (!SQL_SUCCEEDED(r) && r != SQL_NO_DATA) {
        auto e = odbc_error("odbc exec", odbc_diag(SQL_HANDLE_STMT, st));
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return e;
    }
    auto rr = read_all_rows(st, rows, nulls);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return rr;
}

// No-parameter convenience for metadata reads (no values to bind).
util::Result<void> run_query(
    SQLHDBC dbc, const std::string& sql,
    std::vector<std::vector<std::string>>& rows,
    std::vector<std::vector<bool>>& nulls,
    SQLULEN max_rows = 0) {
    static const std::vector<BoundParam> kNoParams;
    return run_query(dbc, sql, kNoParams, rows, nulls, max_rows);
}
```

- [ ] **Step 4: Convert pk_where_clause and load_current_row to bound params**

Rewrite `pk_where_clause` to emit placeholders and append params:
```cpp
std::string pk_where_clause(const std::string& q, const OdbcTable& tbl,
                            const OdbcTable::PkRow& pk,
                            std::vector<BoundParam>& out) {
    std::string s;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) s += " AND ";
        s += quote_ident(q, tbl.pk_columns[i]) + " = ?";
        out.push_back(param_for(tbl, tbl.pk_columns[i], pk.values[i]));
    }
    return s;
}
```
In `load_current_row`, build the params and pass them:
```cpp
    std::vector<BoundParam> params;
    const std::string sql =
        "SELECT * FROM " + quote_ident(q, tbl->sql_table) + " WHERE " +
        pk_where_clause(q, *tbl, tbl->pk_snapshot[idx], params);
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>>        nulls;
    auto r = run_query(dbc, sql, params, rows, nulls, /*max_rows=*/1);
```
(`load_pk_snapshot` has no values — it keeps calling the no-param overload.)

- [ ] **Step 5: Build and run the read regression**

Build, then:
```
pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify
```
Expected: PASS — read/nav unchanged. (Write/seek still use the old literal path; they remain green because the literal helpers are still present.)

- [ ] **Step 6: Commit**

```
git add src/sql_backend/odbc_connection.cpp
git commit -m "refactor(odbc): bind read/navigation WHERE values as parameters

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Convert seek to bound parameters

**Files:**
- Modify: `src/sql_backend/odbc_connection.cpp` (`seek_index`, lines 664-737)
- Test: `tests/unit/abi_plus_odbc_bind_test.cpp` (create — first case: seek with a quote in the key)
- Modify: test target source list to compile the new test

**Interfaces:**
- Consumes: `BoundParam`, `param_for`, `run_query(params)` from Task 2.

- [ ] **Step 1: Write the failing live test (seek key with a quote)**

Create `tests/unit/abi_plus_odbc_bind_test.cpp` (mirror the connect/open helpers from `abi_plus_odbc_write_test.cpp`):
```cpp
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_ODBC)

namespace {
const char* odbc_connstr() {
    const char* v = std::getenv("OPENADS_TEST_ODBC_CONNSTR");
    return (v && v[0]) ? v : nullptr;
}
ADSHANDLE connect_odbc(const char* connstr) {
    const std::string uri = std::string("odbc://") + connstr;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}
ADSHANDLE open_clientes(ADSHANDLE hConn) {
    UNSIGNED8 t[32] = "clientes";
    ADSHANDLE h = 0;
    REQUIRE(AdsOpenTable(hConn, t, t, ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &h) == 0);
    return h;
}
void set_str(ADSHANDLE h, const char* f, const char* v) {
    UNSIGNED8 fb[64]; std::memcpy(fb, f, std::strlen(f) + 1);
    UNSIGNED8 vb[256]; std::memcpy(vb, v, std::strlen(v) + 1);
    REQUIRE(AdsSetString(h, fb, vb, static_cast<UNSIGNED32>(std::strlen(v))) == 0);
}
std::string read_str(ADSHANDLE h, const char* f) {
    UNSIGNED8 fb[64]; std::memcpy(fb, f, std::strlen(f) + 1);
    UNSIGNED8 buf[256] = {0}; UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(h, fb, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}
UNSIGNED32 count_rows(ADSHANDLE h) {
    UNSIGNED32 n = 0; REQUIRE(AdsGetRecordCount(h, 0, &n) == 0); return n;
}
} // namespace

TEST_CASE("ABI: odbc seek key with embedded quote binds safely") {
    const char* cs = odbc_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping"); return; }
    ADSHANDLE hConn = connect_odbc(cs);
    ADSHANDLE h = open_clientes(hConn);

    // Append a row whose name contains a single quote, then seek it by name.
    REQUIRE(AdsAppendRecord(h) == 0);
    set_str(h, "id", "7");
    set_str(h, "nome", "O'Brien");
    set_str(h, "saldo", "5");
    REQUIRE(AdsWriteRecord(h) == 0);

    UNSIGNED8 idx[64] = "nome";
    UNSIGNED8 key[64] = "O'Brien";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(h, key, static_cast<UNSIGNED16>(std::strlen("O'Brien")),
                    ADS_SEEKEQ, &found) == 0);
    CHECK(found == 1);
    CHECK(read_str(h, "nome").find("O'Brien") != std::string::npos);

    REQUIRE(AdsDeleteRecord(h) == 0);            // restore to 3 rows
    CHECK(count_rows(h) == 3);
    REQUIRE(AdsCloseTable(h) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#endif
```
Note: `AdsSeek` builds an index on `nome` first if required by the engine path; if the harness needs an explicit index, follow the existing seek test (`abi_plus_odbc_seek_test.cpp`) for the index-creation calls and replicate them here.

- [ ] **Step 2: Run to verify it fails**

Add the file to the test target. Build, then run live (Access fixture):
```
pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify
```
Expected: FAIL — with the literal path, `O'Brien` is escaped to `'O''Brien'` and may match, but the appended `saldo='5'` into a FLOAT column via the old literal path is the real failure surface; if it passes by luck, the test still asserts the bound behavior that Task 3 guarantees. (If it passes pre-change, proceed — the conversion below is still required for correctness across drivers.)

- [ ] **Step 3: Convert seek_index to bound params**

In `seek_index`, replace the literal `esc` construction and the SQL builders. Remove the `esc` variable; build a single-param vector:
```cpp
    const std::string& q      = impl_->quote;
    const std::string  pkcols = pk_select_list(q, *tbl);
    const std::string  qexpr  = index_column_sql(q, sql_col, kind);
    const std::string  from   = " FROM " + quote_ident(q, tbl->sql_table);

    std::vector<BoundParam> params;
    if (kind == IndexExprKind::UpperColumn) {
        BoundParam p; p.sql_type = SQL_VARCHAR; p.value = key;
        params.push_back(p);
    } else {
        params.push_back(param_for(*tbl, sql_col, key));
    }

    std::string sql;
    if (last_key) {
        sql = soft
            ? "SELECT " + pkcols + from + " WHERE " + qexpr + " <= ?"
              " ORDER BY " + qexpr + " DESC"
            : "SELECT " + pkcols + from + " WHERE " + qexpr + " = ?"
              " ORDER BY " + qexpr + " DESC";
    } else {
        sql = soft
            ? "SELECT " + pkcols + from + " WHERE " + qexpr + " >= ?"
              " ORDER BY " + qexpr + " ASC"
            : "SELECT " + pkcols + from + " WHERE " + qexpr + " = ?";
    }

    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>>        nulls;
    auto r = run_query(impl_->dbc, sql, params, rows, nulls, /*max_rows=*/1);
```
(The rest of `seek_index` — snapshot match, `load_current_row`, `last_seek_found` — is unchanged.)

- [ ] **Step 4: Build and run to verify pass**

```
pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify
```
Expected: PASS — the new seek case and the existing seek/read/write cases.

- [ ] **Step 5: Commit**

```
git add src/sql_backend/odbc_connection.cpp tests/unit/abi_plus_odbc_bind_test.cpp <test-cmake-file>
git commit -m "feat(odbc): bind seek key as a parameter

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Convert write path + NULL-on-write rule; remove literal helpers

**Files:**
- Modify: `src/sql_backend/odbc_connection.cpp` (`flush_table` 806-872; `delete_record` 874-899; remove `escape_literal` 151-159, `is_numeric_literal` 165-174, `format_literal` 176-186)
- Test: `tests/unit/abi_plus_odbc_bind_test.cpp` (add NULL, empty-numeric→NULL, unicode cases)

**Interfaces:**
- Consumes: `param_for`, `pk_where_clause(...,out)`, `run_query(params)` from Task 2.

- [ ] **Step 1: Write failing live tests for NULL / empty-numeric / unicode**

Append to `tests/unit/abi_plus_odbc_bind_test.cpp`:
```cpp
TEST_CASE("ABI: odbc write binds NULL and unicode correctly") {
    const char* cs = odbc_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping"); return; }
    ADSHANDLE hConn = connect_odbc(cs);
    ADSHANDLE h = open_clientes(hConn);

    // Append id=8 with a unicode name and an EMPTY saldo (numeric) -> NULL.
    REQUIRE(AdsAppendRecord(h) == 0);
    set_str(h, "id", "8");
    set_str(h, "nome", "Jo\xC3\xA3o");   // "João" UTF-8
    set_str(h, "saldo", "");              // empty numeric -> SQL NULL
    REQUIRE(AdsWriteRecord(h) == 0);

    // Re-find it and verify: nome round-trips, saldo reads back NULL (empty).
    UNSIGNED8 idx[64] = "id";
    UNSIGNED8 key[8]  = "8";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(h, key, 1, ADS_SEEKEQ, &found) == 0);
    CHECK(found == 1);
    CHECK(read_str(h, "nome").find("Jo\xC3\xA3o") != std::string::npos);
    CHECK(read_str(h, "saldo").empty());   // NULL reads as empty string

    REQUIRE(AdsDeleteRecord(h) == 0);
    CHECK(count_rows(h) == 3);
    REQUIRE(AdsCloseTable(h) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Build, then:
```
pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify
```
Expected: FAIL — the old `format_literal` emits `saldo = ''` for the empty numeric, which the driver rejects (or stores a non-NULL), so the append errors or `saldo` does not read back empty.

- [ ] **Step 3: Convert flush_table INSERT/UPDATE to bound params + NULL rule**

In `flush_table`, the append branch:
```cpp
    if (tbl->appending) {
        if (tbl->staged.empty()) {
            return util::Error{5001, 0, "append with no fields set", tbl->name};
        }
        std::string cols, marks;
        std::vector<BoundParam> params;
        for (std::size_t i = 0; i < tbl->staged.size(); ++i) {
            if (i) { cols += ", "; marks += ", "; }
            cols  += quote_ident(q, tbl->staged[i].first);
            marks += "?";
            params.push_back(param_for(*tbl, tbl->staged[i].first,
                                       tbl->staged[i].second));
        }
        const std::string sql =
            "INSERT INTO " + quote_ident(q, tbl->sql_table) +
            " (" + cols + ") VALUES (" + marks + ")";
        std::vector<std::vector<std::string>> rows;
        std::vector<std::vector<bool>>        nulls;
        if (auto r = run_query(impl_->dbc, sql, params, rows, nulls); !r) {
            return r.error();
        }
    } else {
        if (tbl->staged.empty()) return util::Result<void>{};
        if (!tbl->positioned || tbl->pos >= tbl->pk_snapshot.size()) {
            return util::Error{5026, 0, "no current record to update", ""};
        }
        std::string sets;
        std::vector<BoundParam> params;
        for (std::size_t i = 0; i < tbl->staged.size(); ++i) {
            if (i) sets += ", ";
            sets += quote_ident(q, tbl->staged[i].first) + " = ?";
            params.push_back(param_for(*tbl, tbl->staged[i].first,
                                       tbl->staged[i].second));
        }
        const std::string where =
            pk_where_clause(q, *tbl, tbl->pk_snapshot[tbl->pos], params);
        const std::string sql =
            "UPDATE " + quote_ident(q, tbl->sql_table) + " SET " + sets +
            " WHERE " + where;
        std::vector<std::vector<std::string>> rows;
        std::vector<std::vector<bool>>        nulls;
        if (auto r = run_query(impl_->dbc, sql, params, rows, nulls); !r) {
            return r.error();
        }
    }
```
(The SET params come first, then `pk_where_clause` appends the WHERE params in order — placeholder order matches param order.)

- [ ] **Step 4: Convert delete_record to bound params**

```cpp
    const std::string& q = impl_->quote;
    std::vector<BoundParam> params;
    const std::string where =
        pk_where_clause(q, *tbl, tbl->pk_snapshot[tbl->pos], params);
    const std::string sql =
        "DELETE FROM " + quote_ident(q, tbl->sql_table) + " WHERE " + where;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>>        nulls;
    if (auto r = run_query(impl_->dbc, sql, params, rows, nulls); !r) {
        return r.error();
    }
```

- [ ] **Step 5: Remove the dead literal helpers**

Delete `escape_literal`, `is_numeric_literal`, and `format_literal` from the anon namespace (lines ~151-186). Build to confirm no remaining references:
```
... cmake --build build\odbc-verify
```
Expected: clean build (no "unused function" or "undefined" errors). If any reference remains, it is a value-axis literal that was missed — convert it to `param_for`.

- [ ] **Step 6: Run the full live suite to verify pass**

```
pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify
```
Expected: PASS — NULL/unicode case + seek/read/write cases. Also run the non-live unit suite for regression:
```
build\odbc-verify\tests\openads_unit_tests.exe
```
Expected: full suite green (528+ plus the new map_column cases).

- [ ] **Step 7: Commit**

```
git add src/sql_backend/odbc_connection.cpp tests/unit/abi_plus_odbc_bind_test.cpp
git commit -m "feat(odbc): bind write values; empty non-text value becomes SQL NULL

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Extended fixture (composite PK + date + decimal) and tests

**Files:**
- Modify: `tools/scripts/run_odbc_tests_live.ps1` (seed `pedidos` after `clientes`)
- Modify: `tools/scripts/run_odbc_tests.ps1` (seed `pedidos` in the Access fixture)
- Test: `tests/unit/abi_plus_odbc_extended_test.cpp` (create)
- Modify: test target source list to compile the new test

**Interfaces:**
- Consumes: the ABI (`AdsOpenTable`/`AdsGetNumFields`/`AdsSeek`) against table `pedidos`.

- [ ] **Step 1: Seed the pedidos fixture in the live runner**

In `run_odbc_tests_live.ps1`, after the `clientes` seed (before `$c.Close()`), add portable standard SQL:
```powershell
Invoke-Sql "DROP TABLE pedidos" $true
Invoke-Sql "CREATE TABLE pedidos (cliente_id INT NOT NULL, item_id INT NOT NULL, qtd DECIMAL(10,2), data DATE, PRIMARY KEY (cliente_id, item_id))"
Invoke-Sql "INSERT INTO pedidos (cliente_id, item_id, qtd, data) VALUES (1, 10, 2.50, DATE '2026-01-15')"
Invoke-Sql "INSERT INTO pedidos (cliente_id, item_id, qtd, data) VALUES (1, 20, 100.00, DATE '2026-02-20')"
Invoke-Sql "INSERT INTO pedidos (cliente_id, item_id, qtd, data) VALUES (2, 10, 7.25, DATE '2026-03-05')"
```
If a target rejects the `DATE 'literal'` form (some drivers want `{d '2026-01-15'}`), the runner already wraps `Invoke-Sql` failures; record such drivers in Task 6's matrix and add a per-driver fallback there.

- [ ] **Step 2: Seed pedidos in the Access fixture script**

Open `run_odbc_tests.ps1`, find the ADOX block that creates `clientes`, and add a `pedidos` table with Jet-compatible types: columns `cliente_id` (adInteger), `item_id` (adInteger), `qtd` (adNumeric, Precision=10, NumericScale=2), `data` (adDate), and a composite primary key on (`cliente_id`,`item_id`) via `catalog.Tables("pedidos").Keys.Append`. Seed the same three rows using `#2026-01-15#`-style Jet date literals. (Match the exact ADOX idiom already used for `clientes` in that file.)

- [ ] **Step 3: Write the failing extended test**

Create `tests/unit/abi_plus_odbc_extended_test.cpp` (reuse connect/read helpers — declare them in this file's anon namespace as in the bind test):
```cpp
#include "doctest.h"
#include "openads/ace.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_ODBC)
namespace {
const char* odbc_connstr() {
    const char* v = std::getenv("OPENADS_TEST_ODBC_CONNSTR");
    return (v && v[0]) ? v : nullptr;
}
ADSHANDLE connect_odbc(const char* connstr) {
    const std::string uri = std::string("odbc://") + connstr;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}
std::string read_str(ADSHANDLE h, const char* f) {
    UNSIGNED8 fb[64]; std::memcpy(fb, f, std::strlen(f) + 1);
    UNSIGNED8 buf[256] = {0}; UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(h, fb, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}
} // namespace

TEST_CASE("ABI: odbc composite PK + date + decimal navigation") {
    const char* cs = odbc_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_ODBC_CONNSTR not set; skipping"); return; }
    ADSHANDLE hConn = connect_odbc(cs);
    UNSIGNED8 t[32] = "pedidos";
    ADSHANDLE h = 0;
    REQUIRE(AdsOpenTable(hConn, t, t, ADS_DEFAULT, 0, 0, 0, ADS_READONLY, &h) == 0);

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(h, 0, &count) == 0);
    CHECK(count == 3);                         // composite-PK snapshot has 3 rows

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(h, &nf) == 0);
    CHECK(nf == 4);

    REQUIRE(AdsGotoTop(h) == 0);               // ordered by (cliente_id,item_id)
    CHECK(read_str(h, "qtd").find("2.5") != std::string::npos);   // decimal precision
    CHECK(read_str(h, "data").find("2026") != std::string::npos); // date round-trips

    REQUIRE(AdsCloseTable(h) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}
#endif
```

- [ ] **Step 4: Run to verify fail, then pass**

Add the test file to the target, build, then:
```
pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify
```
Expected: first run FAILs only if seeding/compile is incomplete; once Steps 1-3 are in place, PASS. Confirm `count==3` proves composite-PK discovery (`SQLPrimaryKeys` returning 2 columns) works.

- [ ] **Step 5: Commit**

```
git add tools/scripts/run_odbc_tests_live.ps1 tools/scripts/run_odbc_tests.ps1 tests/unit/abi_plus_odbc_extended_test.cpp <test-cmake-file>
git commit -m "test(odbc): composite-PK + date + decimal fixture and navigation case

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Driver compatibility matrix (live verification + doc)

**Files:**
- Modify: `docs/openads-plus/ODBC_LIVE_TARGETS.md` (add the matrix)

**Interfaces:** none (verification + documentation only).

- [ ] **Step 1: Inventory installed ODBC drivers**

```
pwsh -c "Get-OdbcDriver | Select-Object -ExpandProperty Name"
```
Record which of these are present: SQL Server (ODBC Driver 18), PostgreSQL Unicode, MariaDB ODBC, Firebird ODBC Driver, Microsoft Access Driver.

- [ ] **Step 2: Run the suite against each available target**

For each driver present, build once (Global Constraints) then:
```
pwsh tools\scripts\run_odbc_tests_live.ps1 -ConnStr '<driver-specific connstr>' -BuildDir build\odbc-verify
```
Use the connstr templates already documented in `ODBC_LIVE_TARGETS.md` / the runner header. The portable PostgreSQL/MariaDB/Firebird servers in the environment can be started per the existing project recipes. Capture pass/fail per case (read / seek / write / NULL / composite+date+decimal).

- [ ] **Step 3: Handle missing drivers honestly**

If a PostgreSQL or MariaDB ODBC driver is NOT installed, STOP and ask the owner for authorization to install it (non-portable system change). Do NOT fake a result. Until authorized, record that target as "not verified — driver absent" in the matrix.

- [ ] **Step 4: Write the matrix into the doc**

Add a section to `docs/openads-plus/ODBC_LIVE_TARGETS.md`:
```markdown
## Driver compatibility matrix (verified 2026-06-22)

| Driver | PK discovery | Quote char | Read/nav | Seek | Write | NULL/empty | Composite+date+decimal |
|--------|--------------|-----------|----------|------|-------|------------|------------------------|
| SQL Server (Driver 18) | SQLPrimaryKeys | `"` | ... | ... | ... | ... | ... |
| Microsoft Access | SQLStatistics | `` ` `` | ... | ... | ... | ... | ... |
| PostgreSQL | ... | ... | ... | ... | ... | ... | ... |
| MariaDB | ... | ... | ... | ... | ... | ... | ... |
| Firebird | ... | ... | ... | ... | ... | ... | ... |

Legend: ✅ pass · ⚠️ pass with note (see below) · ❌ fail · — not verified (driver absent).
```
Fill cells from the actual runs. Add a notes list for any ⚠️ (e.g. date-literal syntax fallback, type-coercion quirks).

- [ ] **Step 5: Commit**

```
git add docs/openads-plus/ODBC_LIVE_TARGETS.md
git commit -m "docs(odbc): driver compatibility matrix from live verification

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final verification

- [ ] Full unit suite green: `build\odbc-verify\tests\openads_unit_tests.exe` (528+ plus new map_column cases).
- [ ] Live suite green on the Access CI fixture: `pwsh tools\scripts\run_odbc_tests.ps1 -BuildDir build\odbc-verify`.
- [ ] Live suite green on at least SQL Server + one open-source target (PostgreSQL or MariaDB).
- [ ] `git push origin pr/openads-plus-odbc` after each task (Global Constraints).
- [ ] Update memory `project_openads_plus_sql_backends` + the central ledger entry (ODBC hardening done) once the matrix is recorded.
```
