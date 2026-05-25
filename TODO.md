# OpenADS — TODO

Items are ordered by priority within each section.
Check off completed work and commit the file update so it stays current.

---

## Data Dictionary (DD) support

### Done
- [x] Text-format DD parse + save (`# OpenADS Data Dictionary v1`):
      TABLE, INDEX, USER, MEMBER, LINK, RI, DBPROP, USERPROP rows;
      atomic write-then-rename save.
- [x] `AdsConnect60` opens a `.add` path — parent dir becomes data dir.
- [x] Full DD ABI CRUD: `AdsDDAddTable`, `AdsDDRemoveTable`,
      `AdsDDCreateUser`, `AdsDDDeleteUser`, `AdsDDAddUserToGroup`,
      `AdsDDRemoveUserFromGroup`, `AdsDDCreate{,Drop,Modify}Link`,
      `AdsDDCreate/RemoveRefIntegrity`, `AdsDDSet/GetDatabaseProperty`,
      `AdsDDGet/SetUserProperty`, `AdsDDCreate`, `AdsDDAddTable90`,
      `AdsDDCreateRefIntegrity62`.
- [x] Binary `.add` reader — `DataDict::load_add_binary_()`.
      Parses all `Table` records from real ADS proprietary files.
      Fixture: `tests/fixtures/adi/pmsys.add`. (2026-05-24)

### Open

- [x] **Binary `.add` write support** — full round-trip mutations on
      ADS proprietary Data Dictionary files. `save_add_binary_()` serializes
      all records back in exact binary layout (524-byte records, 2200-byte
      header updated in-place). `add_table`, `remove_table`, `add_index_file`,
      `remove_index_file`, `create_user`, `delete_user` all dispatch through
      the binary path when `binary_format_` is set. 4 round-trip tests added
      to `tests/unit/data_dict_test.cpp`. (2026-05-24)

- [x] **`AdsDDGetTableProperty` / `AdsDDSetTableProperty`**.
      Exported. Handles: RELATIVE_PATH (211), TABLE_PATH (205),
      TABLE_TYPE (204, inferred from extension), CHAR_TYPE (212),
      OBJ_ID (208), FIELD_COUNT (206, returns 0), and the numeric-zero
      boolean properties. Set returns AE_FUNCTION_NOT_AVAILABLE.
      5 unit tests in `tests/unit/abi_dd_table_prop_test.cpp`. (2026-05-24)

- [x] **`AdsDDSetUserProperty`** — implemented and exported.
      Property code dispatch: 1102 (GROUP_MEMBERSHIP) → `add_user_to_group`;
      1103 (BAD_LOGINS) → read-only no-op; all other codes (including
      1101 PASSWORD, 1 COMMENT) stored as `"prop_N"` string properties,
      symmetrical with `AdsDDGetUserProperty`. `AdsDDCreateUser` now
      stores password (`prop_1101`), description (`prop_1`), and optional
      group membership. 7 tests in `tests/unit/abi_dd_user_prop_test.cpp`.
      Cross-checked against `f:\harbour3.2-bcc7.3\contrib\rddads\`. (2026-05-24)

- [x] **ADI level-2 page navigation** — fixed.
      Character-key ADI indexes (CICHAR/CHAR fields) use level-2 dense-leaf
      pages instead of level-3. Branch entries use a different format:
      `padded_key[(len+3)&~3] + cum[4 LE] + page[1]` vs numeric's
      `key[8 BE] + cum[4 BE] + page[4 BE]`. Compound-key tags (e.g. "F2;F14")
      are now parsed and their total key length computed correctly.
      `is_dense_leaf()` now accepts both level-2 and level-3 pages.
      `encode_adt_key` handles CICHAR/CHAR (returns raw field bytes).
      2 new unit tests in `abi_adi_smoke_test.cpp`. (2026-05-24)

- [ ] **ADS proprietary ADT encryption** — out of scope for now.
      We use our own AES encryption (M11.2). ADS-original per-table
      encryption format not reversed; tables with that flag will open
      but return garbled values. Deferred indefinitely.

---

## ADT / ADM driver

### Done

- [x] **`AdsPackTable` / `AdsZapTable`** for ADT.
      `platform::File::truncate()` added (Win32 + POSIX). `AdtDriver::zap()`
      truncates the file to `hdr_len` after zeroing the record count, so
      `Table::pack()` (zap + re-append survivors) leaves no stale bytes.
      Tests in `tests/unit/abi_zap_pack_test.cpp` verify record count,
      field values, and exact file size post-operation. (2026-05-24)

### Open

---

## VFP driver

### Open

- [ ] **VFP table support** (DBF `0x30` / `0x31`).
      `table.cpp:49` returns an error for VFP-typed DBF files. Was in
      the original M4 plan but deferred. Needs a `VfpDriver` that
      handles the `_NullFlags` system field for NULL bitmap and the VFP
      autoinc field type.

---

## SQL engine

### Open

- [x] **`CONTAINS` / `LIKE` in join-cursor and aggregate `WHERE`**.
      Both the join-cursor compile path and the aggregate `FILTER`
      compile path only handle `Cmp / AND / OR / NOT` — anything else
      returns `AE_FUNCTION_NOT_AVAILABLE`. `CONTAINS` and `LIKE` are
      the most common missing operators. Fixed in `ace_exports.cpp`:
      join-cursor compile lambda, aggregate FILTER `cf` lambda, and
      CASE WHEN `compile_cond` lambda all now support LIKE (strip
      trailing spaces + `sql_like_match`) and CONTAINS (load `.fts`
      via `Fts::load/search` before building the lambda; capture the
      hit set by `shared_ptr`). 3 new tests in
      `tests/unit/abi_sql_contains_test.cpp`. (2026-05-24)

- [x] **`CASE WHEN` conditions beyond `Cmp/AND/OR/NOT`**.
      Fixed: `compile_cond` lambda in `ace_exports.cpp` now handles
      `Kind::In` (literal list), `IsNull`/`IsNotNull`, and `Between`
      in addition to the existing `Cmp/AND/OR/NOT`. (2026-05-25)

- [x] **FTS query-time token lookup**.
      Already wired: `CONTAINS(col, expr)` is handled in all four
      SQL compile lambdas (main SELECT WHERE, join-cursor, aggregate
      FILTER, CASE WHEN). Token lookup hits the `.fts` inverted index
      at compile time and captures the record set. (2026-05-24)

---

## Encryption

### Open

- [ ] **`AdsDecryptTable` / `AdsEncryptRecord` / `AdsDecryptRecord`** — out of scope.
      We use our own encryption model (M11.2 AES). ADS-original per-record
      encryption format not reversed. Stubs return `AE_FUNCTION_NOT_AVAILABLE`.

- [ ] **Key derivation hardening**.
      `Connection::set_encryption_password` zero-pads the password to
      32 bytes (noted in `connection.cpp:510`). Should use PBKDF2 or
      Argon2 once a SHA-256 implementation is in the tree. Low urgency
      for local-use scenarios; critical before any multi-user deployment
      that stores encrypted tables long-term.

---

## Transactions

### Open

- [ ] **WAL crash recovery**.
      The engine has `TxLog` / `LsnMap` infrastructure and the WAL
      file is written, but the M5 comment in `ace_exports.cpp` says
      "in-memory; WAL persistence pending." A clean crash-recovery path
      (replay uncommitted WAL on next open, discard partial writes) has
      not been tested end-to-end.

---

## ABI completeness

### Open

- [x] **`AdsEval*Expr` family** — server-side expression evaluation
      used by Harbour/X# `ADSRDD.prg` server-side query path.
      Implemented: `AdsEvalLogicalExpr` (AOF boolean expression at
      current record via `aof::evaluate_record`), `AdsEvalNumericExpr`
      (field read or numeric literal parse), `AdsEvalStringExpr`
      (field read or string literal passthrough). (2026-05-25)

- [ ] **`AdsStmt*` helpers** — used by the X# SQL surface.

- [ ] **`AdsRestructureTable` type conversion** — rename + retype is
      deferred; apps that need it can issue DELETE + ADD for now.

- [x] **`AdsContinue`** (LOCATE/CONTINUE loop).
      Implemented: filter-aware skip(1) on the underlying Table — since
      `Table::skip()` already walks past non-matching records when a
      filter or AOF bitmap is active, `AdsContinue` is a single-step
      forward move with `*pbFound = eof() ? 0 : 1`. Test in
      `tests/unit/abi_aof_test.cpp`. (2026-05-24)

- [ ] **Table-management stubs**.
      `AdsCopyTableContent`, `AdsCloneTable`, `AdsCopyTableStructure`
      all return `AE_FUNCTION_NOT_AVAILABLE`. Needed by migration tools
      and some DD-aware GUI clients.

- [ ] **Enumeration stubs**.
      `AdsGetAllTables`, `AdsGetAllIndexes`, `AdsGetFTSIndexes` return
      zero-count stubs. DD-aware GUI tools (Studio, ADS Data Architect
      equivalents) call these to populate their object trees.

---

## Wire protocol

### Open

- [ ] **Forward-only prefetch (M12.21)** — disabled in M12.21b after
      cursor-drift regressions on indexed scans. Re-enable once the
      indexed-scan drift is understood and fixed.
