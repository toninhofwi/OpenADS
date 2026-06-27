# OpenADS Data Dictionary v2 — Design Document

**Status:** Approved for implementation (Fase 3.1)  
**Date:** 2026-06-27  
**Authors:** OpenADS core team  
**Related:** `TODO.md` (DD section), `src/engine/data_dict.{h,cpp}`, `tools/import_dd/`

---

## 1. Problem

OpenADS currently supports two incompatible DD on-disk representations:

| Format | Signature | Writable by OpenADS | Permission enforcement |
|--------|-----------|---------------------|------------------------|
| **SAP proprietary** | `ADS Data Dictionary` | Partial (import tool only) | Requires `import_dd` — encrypted blobs undecodable offline |
| **OpenADS native (v2)** | `Advantage Table` (ADT header) | Full round-trip | Native bitmask ACL + in-memory cache |

Maintaining SAP binary compatibility causes regressions: fixing one DD feature often breaks another because SAP record layouts, cipher blocks, and continuation memos are undocumented and change across ADS versions.

**Decision (Fase 3):** OpenADS owns the on-disk format. SAP `.add` files are **imported once** via `import_dd`; thereafter only the OpenADS native format is read or written. File extensions remain SAP-compatible (`.add` / `.am` / `.ai`) so tools and users recognise a data dictionary, but the internal record structure is ours.

---

## 2. Goals and non-goals

### Goals

- Store all DD objects (tables, indexes, users, groups, memberships, links, RI, triggers, procedures, functions, views, field properties, database properties, permissions) in a **single object table** with JSON payloads in `.am`.
- Support **parent/child** relationships via `PARENT_ID` (e.g. trigger → table, field property → table).
- Persist **fine-grained permissions** as compact bitmasks; resolve effective rights in **O(1)** per object after session connect.
- **Full CRUD** through existing `AdsDD*` ABI without SAP DLL at runtime.
- **One-shot migration** from SAP via `tools/import_dd` (Windows/Linux only; requires SAP ACE DLL).
- **DaWeb** and `system.*` virtual tables read the same in-memory `DataDict` model.

### Non-goals

- Round-trip read/write of SAP proprietary binary layout (read-only for import; no new SAP writes).
- Offline decryption of SAP permission cipher blocks (documented as impossible without SAP DLL).
- Changing file extensions (`.add` / `.am` / `.ai` stay as-is).
- DD replication or multi-master sync (future work).

---

## 3. File layout

Three companion files per database, same naming convention as SAP ADS:

```
mydb.add   — object index (ADT-compatible fixed records)
mydb.am    — memo store (ADM-compatible blocks, JSON blobs)
mydb.ai    — compound index on .add (optional; ADI-compatible)
```

### 3.1 `.add` — object table

Uses a standard **ADT table header** so existing ADT tooling can inspect the file:

```
Offset  Size   Content
0       400    ADT header ("Advantage Table" signature)
400     1200   6 × 200-byte field descriptors
1600+   N×342  Fixed-length records
```

**Record layout (342 bytes):**

| Offset | Field | Type | Description |
|--------|-------|------|-------------|
| 0 | del | byte | `0x04` = active, `0x05` = deleted |
| 1–4 | nullmap | 4 bytes | Always zero |
| 5–8 | OBJ_ID | uint32 LE | Unique object id (monotonic allocator) |
| 9–12 | PARENT_ID | uint32 LE | Parent object id, or 0 |
| 13–32 | OBJ_TYPE | CHAR(20) | Object kind (see §4) |
| 33–232 | OBJ_NAME | CHAR(200) | Primary name / lookup key |
| 233–332 | OBJ_KEY | CHAR(100) | Secondary key (path, grantee, field…) |
| 333–341 | OBJ_DATA | Memo(9) | `block_no` (u32 LE) + `data_len` (u32 LE) + `0x00` → JSON in `.am` |

**Save semantics:** atomic write-then-rename (`.add.tmp` → `.add`, same for `.am`).

### 3.2 `.am` — JSON memo store

ADM block size **256 bytes**. Each `OBJ_DATA` memo reference points to a contiguous JSON blob.

JSON objects carry `"fmt":1` (schema version) and type-specific fields. Large SQL bodies (triggers, procedures) live entirely in memo JSON; no SAP-style NUL-delimited continuation chains.

### 3.3 `.ai` — index file

Optional ADI compound index on `(OBJ_TYPE, OBJ_NAME)` for fast `AdsDDFindFirstObject` / `AdsDDFindNextObject`. Not required for correctness; server loads all records into memory on connect.

---

## 4. Object types

| OBJ_TYPE | OBJ_NAME | OBJ_KEY | PARENT_ID | JSON payload |
|----------|----------|---------|-----------|--------------|
| `Table` | alias | relative_path | 0 | `{pk, default_idx, comment}` |
| `Index` | table_alias | index_path | table OBJ_ID | `{comment}` |
| `User` | username | — | 0 | `{prop_<code>: value, …}` |
| `Group` | groupname | — | 0 | `{}` |
| `Member` | username | groupname | 0 | `{}` |
| `Link` | alias | — | 0 | `{path, user, pwd}` |
| `RI` | rule_name | — | 0 | `{parent, child, parent_tag, child_tag, update_opt, delete_opt, fail_table}` |
| `DbProp` | prop_key | — | 0 | `{value}` |
| `FieldProp` | table_alias | field_name | table OBJ_ID | `{required, default, rule, …}` |
| `Perm` | object_name | grantee | 0 | `{obj_type, bitmask}` |
| `Trigger` | `table::name` | — | table OBJ_ID | trigger JSON blob (§5.3) |
| `Proc` | proc_name | — | 0 | proc JSON blob |
| `Function` | func_name | — | 0 | function JSON blob |
| `View` | view_name | — | 0 | `{sql, comment}` |

**OBJ_ID allocation:** `max(existing OBJ_ID) + 1` on create; never reuse deleted ids in v2.0 (garbage-collect in a future compaction tool).

**Built-in groups:** `DB:Public`, `DB:Admin`, `DB:Backup`, `DB:Debug` are created on demand when referenced (`add_user_to_group` auto-creates the `Group` record).

---

## 5. Permissions model

### 5.1 Bitmask layout

Stored in `Perm` records as JSON `"bitmask": <uint32>`. Constants in `DataDict` (`data_dict.h`):

| Bit | Constant | Meaning |
|-----|----------|---------|
| 0x0001 | `DD_PERM_SELECT` | Read rows |
| 0x0002 | `DD_PERM_UPDATE` | Modify rows |
| 0x0004 | `DD_PERM_EXECUTE` | Call procedure/function |
| 0x0010 | `DD_PERM_INSERT` | Insert rows |
| 0x0020 | `DD_PERM_DELETE` | Delete rows |
| 0x0040 | `DD_PERM_REFERENCE` | FK parent reference |
| 0x0080 | `DD_PERM_GRANT` | May re-grant (WITH GRANT) |
| 0x80000000 | `DD_PERM_FULL` | All operations (INHERIT / full group) |

`GRANT` / `REVOKE` SQL and `AdsDDSetUserTableRights` map to these bits. Legacy 0–4 table levels map as:

| Level | Bits |
|-------|------|
| 0 | none |
| 1 | SELECT |
| 2 | SELECT + UPDATE + INSERT |
| 3 | + DELETE |
| 4 | FULL (`0x80000000`) |

### 5.2 Effective permissions

For user `U`:

```
effective_bits(O) = OR( direct_grants(U, O), OR( group_grants(G, O) for G in groups(U) ) )
```

**Server-side cache** (`DataDict::build_perm_cache`):

- Called once per authenticated session after `AdsConnect60` (already wired in `ace_exports.cpp`).
- Produces `username → object_name → merged_bits`.
- Invalidated on any `grant_permission()` / `set_table_permission()` / membership change.
- `check_perm()` and `get_effective_ops()` are **O(1)** after cache build.

**Default policy:** If no `Perm` records exist for the DD, all operations are allowed (open access). If any ACL exists, objects without an explicit grant are unrestricted; objects with grants require matching bits.

### 5.3 Field-level permissions

Field grants use `Perm` with `object_name = "table.field"` and `obj_type = "Field"`. Enforcement points:

- `AdsOpenTable` — table-level open bit
- `AdsReadRecord` / `AdsWriteRecord` — field mask (future: column filter in wire protocol)
- `system.permissions` / `system.effectivepermissions` — expose 0/1/2 per operation

---

## 6. JSON schemas (fmt=1)

### Trigger

```json
{
  "fmt": 1,
  "type": "Trigger",
  "name": "trg_audit",
  "table": "orders",
  "event": 7,
  "timing": 1,
  "priority": 0,
  "enabled": true,
  "container": "SQL",
  "procedure": "BEGIN … END",
  "comment": "",
  "options": 3
}
```

### Stored procedure / function

Same pattern as triggers; `input_params` / `output_params` as structured strings (pipe-delimited name:type, compatible with DaWeb editor).

### View

```json
{"fmt":1, "sql":"SELECT …", "comment":""}
```

---

## 7. Runtime behaviour

### 7.1 Connect path

```
AdsConnect60(path.add)
  → DataDict::open(path)
      if signature == "ADS Data Dictionary"
          load_add_binary_()  [import tool only path]
          if has_sap_permissions() → AE_SAP_PERMS_NEED_IMPORT (5174)
      if signature == "Advantage Table"
          load native records + .am JSON
  → build_perm_cache(connected_user)
```

### 7.2 Enforcement hooks (already implemented)

| Feature | Hook |
|---------|------|
| RI | `AdsWriteRecord`, `AdsDeleteRecord` |
| Triggers | before/after insert/update/delete in table engine |
| Permissions | `AdsOpenTable`, SQL `GRANT`/`REVOKE` |
| Virtual tables | `system.permissions`, `system.effectivepermissions`, `system.usergroupmembers` |

### 7.3 Server memory

On connect the server holds one `DataDict` instance per database path. All objects are in RAM (`unordered_map` / `vector` members in `DataDict`). Permission cache is per-user, lazy or eager. No re-read from disk until `save()` or external file change (not hot-reloaded in v2.0).

---

## 8. Migration from SAP (`import_dd`)

**Input:** SAP `.add` + `.am` (and optionally `.ai`)  
**Output:** Copy of `.add` rewritten in OpenADS native format with:

1. All tables, indexes, users, groups (from SAP SQL + `AdsDDGet*` calls)
2. Group memberships (from `system.usergroupmembers` via SAP ACE)
3. ACL rows (from `system.permissions` via SAP ACE)
4. Triggers, procedures, RI, links (bodies from `.am` continuation)
5. `has_sap_permissions()` = false on output file

**Platform:** Windows or Linux with SAP `ace32.dll` / `libace.so`. macOS cannot run import (no SAP ACE).

**Post-import:** `AdsConnect60` succeeds; OpenADS enforces permissions natively.

### 8.1 import_dd changes (Fase 3.2)

Current tool writes permissions into a **copy** of the SAP file structure. Target behaviour:

- [ ] Emit **native ADT object records** instead of patching SAP binary
- [ ] Map every SAP `AdsDDGet*` / `system.*` property to v2 JSON fields
- [ ] Verification pass: reopen with OpenADS-only `DataDict::open`, compare object counts
- [ ] CLI flag `--native-v2` (default on) for explicit format selection

---

## 9. Implementation status

| Component | Status | Location |
|-----------|--------|----------|
| Native `.add` / `.am` read/write | Done | `data_dict.cpp` |
| Object types (table…view) | Done | `DataDict` API |
| Permission bitmasks + cache | Done | `build_perm_cache`, `check_perm` |
| SAP binary read (import only) | Done | `load_add_binary_()` |
| Connect rejection 5174 | Done | `ace_exports.cpp` |
| `import_dd` → native v2 output | **Pending** | Fase 3.2 |
| DD v2 test suite | **Pending** | Fase 3.3 |
| DaWeb native-only reader | **Pending** | Fase 3.4 |
| `.ai` index maintenance | Optional | not started |

---

## 10. Test plan (Fase 3.3)

New file: `tests/unit/dd_v2_suite_test.cpp` (or extend `data_dict_test.cpp`).

| Test | Asserts |
|------|---------|
| `create_empty_dd` | Signature, header dims, zero records |
| `roundtrip_all_object_types` | create → save → reopen → equality |
| `parent_id_trigger` | Trigger PARENT_ID points to table OBJ_ID |
| `permissions_effective` | user + group grants merge correctly |
| `permissions_denied` | `check_perm` false when bit missing |
| `grant_revoke_sql` | SQL persists to `Perm` records |
| `import_dd_native` | SAP fixture → native v2 → connect OK (fixture-gated) |
| `atomic_save` | crash simulation leaves old file intact |

---

## 11. DaWeb changes (Fase 3.4)

- Stop reading SAP binary layouts in `api/trigger_body.php` and siblings.
- Use OpenADS ACE / HTTP API to list objects from `DataDict` in-memory model.
- RI dropdown populated from `RI` objects (not hard-coded).
- User groups: show `DB:Public`, `DB:Admin`, etc. from `Group` + `Member` records.

---

## 12. PR plan

| PR | Scope | Depends on |
|----|-------|------------|
| **3.1** | This design doc + plan update | — |
| **3.2** | `import_dd` writes native v2 | 3.1 |
| **3.3** | DD v2 test suite | 3.2 |
| **3.4** | DaWeb native reader | 3.2 |
| **3.5** | README DD section + `docs/en/migrating-from-ads.md` update | 3.3 |

---

## 13. Open questions

1. **OBJ_ID compaction** — reclaim ids from deleted objects? Defer to maintenance tool.
2. **Hot reload** — `AdsDDReload` or file watcher? Not in v2.0; restart server after DD edit.
3. **Encrypted passwords** — store hashed only (bcrypt/scrypt) in User JSON `prop_1101`; migration from SAP plaintext on import.
4. **Multi-database links** — `Link` objects already support remote paths; credential storage policy TBD.

---

## References

- `src/engine/data_dict.h` — canonical type definitions and bitmask constants
- `tools/import_dd/README.md` — migration workflow
- SAP ADS 10.10 Help — functional requirements (not layout)
- `docs/plan-ejecucion.md` — Fase 3 roadmap