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

- [ ] **Guard mutations on binary-format DDs** (highest priority).
      `save()` currently overwrites with OpenADS text format when a
      binary `.add` was loaded. This silently corrupts any real ADS DD
      (e.g. pmsys.add) if any mutation ABI call is made through
      OpenADS. Options: (a) mark binary DDs read-only and return
      `AE_DD_CANNOT_MODIFY` on mutations, or (b) implement binary write.

- [ ] **`AdsDDGetTableProperty` / `AdsDDSetTableProperty`**.
      Not yet exported. Real ACE apps use these to read the registered
      path, table type, character set, etc. for a table alias.
      Signature (from ace.h conventions):
      `AdsDDGetTableProperty(hConn, pucTable, usProp, pBuf, pusLen)`

- [ ] **`AdsDDSetUserProperty`** property-code mapping.
      The thunk calls `set_user_property` with a raw `usProp` numeric
      code. Verify the code ↔ key mapping is consistent with what
      Harbour / X# actually sends (cross-check against rddads source).

- [ ] **ADI level-2 page navigation** (deferred).
      Causes error 6106 on tables with large/complex index files
      (e.g. `leases.adi` with 23 tags). Level 1 (branch) and Level 3
      (dense leaf) already work; only the intermediate level is missing.
      See format notes in `src/drivers/adi/adi_driver.cpp`.

- [ ] **ADS proprietary ADT encryption**.
      Different from OpenADS M11.2 AES encryption. Format not yet
      reverse-engineered. Tables with the encryption flag set will open
      but return garbled field values.

---

## ADT / ADM driver

### Open

- [ ] **`AdsPackTable` / `AdsZapTable`** for ADT.
      Currently stubbed as `AE_FUNCTION_NOT_AVAILABLE`. Need to
      compact the ADT by rewriting active records, rebuilding the
      header, and truncating the file.

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

- [ ] **`CONTAINS` / `LIKE` in join-cursor and aggregate `WHERE`**.
      Both the join-cursor compile path and the aggregate `FILTER`
      compile path only handle `Cmp / AND / OR / NOT` — anything else
      returns `AE_FUNCTION_NOT_AVAILABLE`. `CONTAINS` and `LIKE` are
      the most common missing operators.

- [ ] **`CASE WHEN` conditions beyond `Cmp/AND/OR/NOT`**.
      Same restriction in the `CASE WHEN` condition compiler. In
      practice this means CASE expressions can only branch on simple
      comparisons today.

- [ ] **FTS query-time token lookup**.
      `AdsCreateFTSIndex` builds the `.fts` inverted-index file, but
      the comment in `ace_exports.cpp` notes "Search support (token
      lookup at query time) is a follow-up milestone." `AdsFTSSearch`
      exists and works; the gap is wiring it to `WHERE CONTAINS(col,
      expr)` inside the SQL engine so full-text predicates can be used
      in ordinary SELECT statements.

---

## Encryption

### Open

- [ ] **`AdsDecryptTable` / `AdsEncryptRecord` / `AdsDecryptRecord`**.
      All three are stubbed `AE_FUNCTION_NOT_AVAILABLE` pending ADS
      encryption-mode reverse-engineering. `AdsEncryptTable` (OpenADS
      format) works; the ADS-original per-record format does not.

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

- [ ] **`AdsEval*Expr` family** — server-side expression evaluation
      used by Harbour/X# `ADSRDD.prg` server-side query path.
      Client-side fallback handles every common case; these are
      nice-to-have for completeness.
      (`AdsEvalLogicalExpr`, `AdsEvalNumericExpr`, `AdsEvalStringExpr`
      all return `AE_FUNCTION_NOT_AVAILABLE`.)

- [ ] **`AdsStmt*` helpers** — used by the X# SQL surface.

- [ ] **`AdsRestructureTable` type conversion** — rename + retype is
      deferred; apps that need it can issue DELETE + ADD for now.

- [ ] **`AdsContinue`** (LOCATE/CONTINUE loop).
      Returns `AE_FUNCTION_NOT_AVAILABLE`; X# runs LOCATE itself but
      Harbour apps calling `CONTINUE` directly will fail.

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
