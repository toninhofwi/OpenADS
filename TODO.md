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

## ABI completeness

### Open

- [ ] **`AdsEval*Expr` family** — server-side expression evaluation
      used by Harbour/X# `ADSRDD.prg` server-side query path.
      Client-side fallback handles every common case; these are
      nice-to-have for completeness.

- [ ] **`AdsStmt*` helpers** — used by the X# SQL surface.

- [ ] **`AdsRestructureTable` type conversion** — rename + retype is
      deferred; apps that need it can issue DELETE + ADD for now.

---

## Wire protocol

### Open

- [ ] **Forward-only prefetch (M12.21)** — disabled in M12.21b after
      cursor-drift regressions on indexed scans. Re-enable once the
      indexed-scan drift is understood and fixed.
