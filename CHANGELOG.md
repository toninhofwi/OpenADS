# Changelog

All notable changes to OpenADS are recorded here. The project follows
[Semantic Versioning](https://semver.org/) once 1.0 ships; until then
0.x.y releases may break the C ABI between minor versions to track the
real ACE SDK.

## Unreleased

Post-v1.0.0-rc6 work, all rddads-compat deepening so a real Harbour
`rddads` `.prg` drives OpenADS through the same edge cases Clipper
exposes against original ACE.

- **M12.22 — versioned ACE overloads for the X# RDD.** Exports the
  `Ads*NN` entry-point names X# binds by name (`AdsConnect26`,
  `AdsCreateTable71` / `90`, `AdsOpenTable90`, `AdsCreateIndex90`,
  `AdsDDAddTable90`, `AdsDDCreateRefIntegrity62`, `AdsFindFirstTable62`
  / `AdsFindNextTable62`, `AdsGetDateFormat60`, `AdsGetExact22`,
  `AdsReindex61`, `AdsRestructureTable90`). Most forward to the base
  signature (dropping the charset / collation / page-size params newer
  ACE builds added); `AdsGetBookmark60` / `AdsGotoBookmark60`
  round-trip the recno as a 4-byte blob; `AdsCancelUpdate90` /
  `AdsSetProperty90` are accepted no-ops; `AdsFindConnection25` /
  `AdsGetTableHandle25` report not-found (OpenADS keys by handle, not
  path / name).
- **M12.23 — close the export gap the X# Advantage RDD relies on.**
  A live run of X#'s `AXDBFCDX` RDD against OpenADS' `ace64.dll`
  surfaced ~45 more entry points `ADSRDD.prg` binds by name
  (`AdsGetMemoBlockSize`, `AdsGetTableOpenOptions`, `AdsGetBookmark`,
  `AdsCancelUpdate`, `AdsSetField`/`AdsSetEmpty`/`AdsSetNull`/
  `AdsSetShort`/`AdsSetMoney`/`AdsSetTime`/`AdsSetTimeStamp`,
  `AdsGetDate`, `AdsContinue`, `AdsEval*Expr`, the RI / unique /
  autoinc enforcement toggles, `AdsStmt*` helpers, …). Added: forwards
  where one fits, accept-and-ignore for session/statement toggles,
  `AE_FUNCTION_NOT_AVAILABLE` for the genuinely-unimplemented (so the
  X# runtime falls back to its own client path). The field-setter
  family handles the ACE "field name *or* 1-based ordinal cast to a
  pointer" idiom. **`AdsAppendRecord` now auto-locks the new record**
  (ACE semantics for non-exclusive tables — X#'s `GoHot` refuses to
  write a record it sees as unlocked), and **`AdsIsRecordLocked` /
  `AdsLockRecord` / `AdsUnlockRecord` honour `recno == 0` = current
  record** and report the real lock state instead of stubbing 0.
  **`AdsCreateIndex61` / `AdsCreateIndex90` option-bit fix:** the
  "descending" flag is `ADS_DESCENDING` (`0x08`), not `0x02` — `0x02`
  is `ADS_COMPOUND`, which X#'s ADSRDD always sets for CDX orders, so
  the old mask built every X# order descending and `DbGoTop` landed on
  the last key. **`AdsCreateTable` / `AdsCreateTable90` now stage an
  empty `.fpt` next to the `.dbf` when the field list has an `M` field**
  (using `usMemoBlockSize`, default 64) — without it `Connection::
  open_table` can't attach a memo store and any memo write fails
  "memo store not attached". With these fixes the full X# `AXDBFCDX`
  smoke (`tests/smoke/xsharp/AdsSmoke.prg`) passes end-to-end against
  OpenADS' `ace64.dll`: `DbCreate` (incl. an M field) → `DbUseArea` →
  `OrdCreate` ×2 → `DbAppend`+`FieldPut` ×4 → `DbCommit` → NAME-order
  `GoTop`/`Skip ±`/`GoBottom`/`Eof` → `DbSeek` hit + miss → memo
  round-trip → `DbDelete`/`DbRecall` → replace a key field + re-read
  through the CITY order → `DbCloseArea`.
- **X# RDD against a remote OpenADS server.** Three more fixes so X#'s
  ADSRDD drives `openads_serverd` over the wire (`AdsConnect60(tcp://
  host:port/<datadir>, ADS_REMOTE_SERVER) → AX_SetConnectionHandle →
  DbUseArea`): `remote_field_index` now honours the "field name OR
  1-based ordinal cast to a pointer" idiom (X#'s `_FieldSub` calls
  `AdsGetFieldType`/`Length`/`Decimals` by ordinal — a tiny pointer
  value was being dereferenced as a string); the remote `AdsOpenTable`
  branch defaults a missing extension to `.dbf` (X# passes the bare
  table name for remote tables); and `AdsGetTableFilename` gained a
  remote path (returning the opened name) instead of failing
  `AE_INTERNAL_ERROR` — X#'s `Open` calls it right after `_FieldSub`.
  New `tests/smoke/xsharp/AdsSmoke_remote.prg` opens `customer.dbf` on
  the server and does read/nav (RecCount / GoTop / Skip ± / GoBottom /
  Eof / FieldGet) — passes against the dev server.
- **Test layout.** Third-party RDD smoke harnesses moved under
  `tests/smoke/` — `tests/smoke/harbour/` (was `tests/harbour_smoke/`)
  plus a new `tests/smoke/xsharp/` (`AdsSmoke.prg` driving OpenADS' DLL
  through X#'s `AXDBFCDX` RDD). GUI showcases (FiveWin, X# WinForms)
  get an `examples/` tree. All opt-in — none run in the default `ctest`
  pass. Added doctest coverage `abi_versioned_overloads_test.cpp` (local)
  + `abi_remote_overloads_test.cpp` (over the wire, gated on
  `OPENADS_TEST_REMOTE`).
- **Clipper-convention empty / past-end / Limbo states.**
  `goto_record(0)` is no-op + Eof (not error 5000); empty table
  reports BOF / EOF + `RecNo() = LastRec()+1`; GO past-end enters
  Limbo; CDX dup-tag silent reopen; CDX dup-key insertion uses recno
  tie-break.
- **Index cursor consistency.** `goto_record` re-syncs the index
  cursor to the row's key (so the next SKIP walks the right
  neighbour); CDX cursor state tracking + `Table::seek_key` last-key
  variant; hard-seek miss parks the cursor on the `>` neighbour for
  SKIP; hard-seek past every key parks at AfterEnd; `GO 0` keeps
  Limbo.
- **`SET DELETED ON` everywhere.** Index walks skip deleted rows;
  natural-order GOTOP / GOBOTTOM skip deleted; all-deleted under
  `SET DELETED` reports Limbo (not Eof); `GOTOP` / `GOBOTTOM` on an
  empty index report Limbo.
- **DESCEND wired through.** `bFindLast` retry on DESCEND retired;
  `DBSEEK( '' )` with `bSoftSeek` lands at the first record + `FOUND
  = .T.`; the empty-key shortcut applies only to ASC orders.
- **CDX FOR-clause filter.** `CREATE INDEX … FOR <expr>` honoured at
  build time and on every subsequent insert; `CREATE INDEX` inserts
  deleted rows too (DBFCDX semantics); each new `CREATE INDEX`
  replaces the active order (Clipper convention); re-`CREATE INDEX`
  with an existing tag clears the old B+tree first; `ADS_DOUBLEKEY`
  ASCII conversion + creation-order tag ordinals.
- **rddads compat patches** (`tools/harbour_patch/`). `adsSeek`
  carve-out: `Seek( '' )` with the soft-seek flag now leaves `fBof`
  alone under Limbo so Harbour's own EOF logic doesn't snap to recno
  0; `ORDSETFOCUS(N)` uses CDX-file insertion order, not handle
  ids; `rddads-compat` default connection + `CREATE INDEX` goto-top.
- **SKIP overshoot.** Cursor stays on the last live record, not
  recno 0.
- Tests: `rddtst-flow` repro for `DBGOTOP` after delete / recall /
  redelete; unit test for `DBGOBOTTOM` + FOR-clause + deletes.

## 1.0.0-rc6 — 2026-05-07

Studio schema view shows the ADS field-type *letter* (C / N / M / D
/ L / I / Y / B / V / Q / G) instead of the numeric code, with the
ADS type-letter mapping corrected; the numeric tooltip is dropped.

## 1.0.0-rc5 — 2026-05-07

Studio binary-memo serializer + Win x86 build fix. Binary memo
cells are now base64-encoded in `/api/tables/<t>/rows` so JSON
round-trips cleanly through the SPA.

## 1.0.0-rc4 — 2026-05-07

CDX leaf+branch full multi-level split + memo > 64 K fix +
cross-platform `-Werror` clean.

- **`M(cdx-split)`** — full multi-level B+tree leaf+branch split.
  Stress harness now exercises 2 bags × 2 tags each at 200 K rows;
  `CREATE INDEX` defaults back to CDX (the previous NTX fallback is
  retired).
- **DBF compat.** Accept 0xF5 / 0xFB headers + single-letter type
  codes; `AdsGetField` no longer truncates memos > 64 K to 65534
  bytes.
- **Cross-platform.** `ace32.lib` / `ace64.lib` import libs +
  ordinal-compat tooling shipped. GCC 13 (Ubuntu 24.04) + clang
  Release pass `-Werror` cleanly: `-Wshadow`,
  `-Wstringop-truncation`, `-Wformat-truncation`, `-Wsign-conversion`
  in `julian_to_ymd`, `COL%zu` field-name copy, stress harnesses,
  `probe_cdx2`, drivers/cdx iterators.
- **Studio.** `studio.web.0.12` ZIP backup of data dir,
  `studio.web.0.13` table-type override + memo hex viewer,
  `studio.web.0.14` host OS / arch / compiler banner.

## 1.0.0-rc3 — 2026-05-07

Studio sessions kill + JSON export + TLS deployment docs.

- **Studio.** `studio.web.0.10` bulk select + saved queries + SQL
  highlight; `studio.web.0.11` kill-session button + JSON export.
- **TLS deployment guide** (EN / ES / PT) — proxy, stunnel, SSH
  tunnel recipes for fronting the cleartext server with TLS until
  M12.12 ships.
- CI release-workflow build timeout bumped 45 → 60 minutes.

## 1.0.0-rc2 — 2026-05-07

Studio web 0.4 – 0.10: Sessions, Data Dictionary tab + REST,
Reindex / Pack / Zap + `CREATE INDEX` wizard + memo viewer, sidecar
list / server stats / DBF upload, HTTP Basic auth + table download
+ theme toggle, Browse sort + filter + i18n (EN / ES / PT). Data
Dictionary documentation pages (EN / ES / PT). `b64decode`
sign-conversion fix.

## 1.0.0-rc1 — 2026-05-07

Studio web console + bilingual+ documentation site + release CI.

- **OpenADS Studio** (`studio.web.0.1` … `0.3`) — clean-room
  ARS-equivalent web console embedded in `serverd`. CRUD + paginated
  browse + server info; `CREATE` / `DROP` / encrypt + SQL history.
  All third-party-product-name references scrubbed from Studio
  copy. `AdsGetLastError` captured before close + 'did you mean'
  hint on connection failures. Docs link in header.
- **Documentation site** — bilingual+ EN / ES / PT under `docs/`,
  published through GitHub Pages workflow. Benchmarks pages +
  Studio screenshots.
- **Release CI** — `release` workflow builds + packages + publishes
  on tag push. `docs/superpowers` and `build/` are export-ignored
  from source archives.

## 0.4.1 — 2026-05-06

`openads_bench` v2 + CI matrix gains a TLS=ON entry.

## 0.4.0 — 2026-05-06

Real TLS transport, transport abstraction, wire-protocol spec.

- **M12.12** — real TLS via vendored mbedtls 3.6 LTS (Apache-2.0).
  The `tls://` URI scheme reserved in 0.3.6 now wires through to a
  real handshake on both client and server.
- **M12.13** — transport abstraction. The wire protocol no longer
  bakes in the socket type; cleartext, TLS, and any future
  transport plug in through a single virtual interface.
  `docs/wire-protocol.md` documents the wire format.
- CI runners + actions bumped to current versions; missing
  `<tuple>` include added.

## 0.3.6 — 2026-05-06

`tls://` URI scheme reserved (M12.12 stub) ahead of the real
handshake landing in 0.4.0.

## 0.3.5 — 2026-05-06

Wire-protocol semantics complete: ACE error-code propagation
(M12.10) and batched row fetch (M12.11 — `Fetch` / `FetchAck`).

## 0.3.4 — 2026-05-06

Phase 2 server feature-complete (read + write + SQL + index +
auth).

- **M12.6** — remote write surface (`append` / `set` / `delete` /
  `recall` / `goto` / `flush`).
- **M12.7** — remote SQL exec (`ExecuteSQL` wire op).
- **M12.8** — remote `AdsReindex`.
- **M12.9** — server-side authentication.
- `RemoteConnection` dtor now calls `disconnect`.

## 0.3.3 — 2026-05-06

Phase 2 server alive end-to-end + cross-platform SQL bench.

- **M12.3** — server accept loop + `Hello` / `Connect` dispatch.
- **M12.4** — remote read-only table ops.
- **M12.5** — dual-mode DLL: `tcp://` URIs route the ABI to the
  server; local-mode paths still hit the in-process engine.
- `openads_serverd` standalone TCP server CLI.
- `openads_bench` cross-platform SQL workload timer.
- macOS bring-up: `setup_macos.sh` one-shot script,
  `HOST_NAME_MAX` fallback, `accept()` self-connect wake-up,
  same-process lock contention test skip.
- Linux bring-up: OFD locks for fd-scope contention,
  `shutdown(SHUT_RDWR)` before `close()` so blocked `accept()`
  wakes, third + fourth waves of clang `-Werror` sign /
  include / unused fixes,
  `-Wreturn-type-c-linkage` cleanup on internal helpers.

## 0.3.2 — 2026-05-05

SQL window-function + scalar-fn deepening + sockets layer.

- **M10.49** PARTITION BY.
- **M10.50** RANK / DENSE_RANK.
- **M10.51** qualified column refs.
- **M10.52** multi-row `VALUES`.
- **M10.53** `NULLIF` / `COALESCE` / `IFNULL`.
- **M10.54** `FILTER (WHERE …)` aggregate clause.
- **M12.2** sockets layer (cross-platform).

## 0.3.1 — 2026-05-05

SQL date / CTE / NULL surface + collation + wire skeleton.

- **M10.43** multi-arg scalar fns.
- **M10.44** `IS NULL` / `IS NOT NULL`.
- **M10.45** date scalar fns.
- **M10.46** derived tables.
- **M10.47** `ROW_NUMBER()`.
- **M10.48** CTE (`WITH …`).
- **M11.6** NULL bitmap.
- **M11.7** collation.
- **M11.8** OEM / UTF-8 conversion.
- **M12.1** wire skeleton.

## 0.3.0 — 2026-05-05

SQL maturity wave: every JOIN flavour, the full subquery /
aggregate / DISTINCT / UNION / CASE / LIMIT surface, plus the AEP
host, AES-256-CTR encrypted DBFs, VFP V / Q field types, and
nested transactions. Brings the SQL layer to "real apps run
through it" status.

### Highlights

- **JOIN matrix.** `INNER` (M10.13 parse + M10.14 executor), `LEFT
  OUTER` (M10.16), `RIGHT OUTER` (M10.21), `FULL OUTER` (M10.22 —
  union of LEFT + RIGHT), `JOIN` + `WHERE` / `ORDER BY` combos
  (M10.20), `JOIN` + aggregate combo (M10.23), `GROUP BY` across
  `JOIN` (M10.34).
- **Subqueries.** `IN` literal lists + subqueries (M10.15),
  `EXISTS` uncorrelated (M10.17), correlated `EXISTS` (M10.24),
  scalar subquery `<col> op (SELECT col FROM t)` (M10.18),
  aggregate scalar subquery (M10.19), correlated scalar subquery
  (M10.29), correlated `IN` subquery (M10.35).
- **Aggregates + grouping.** `COUNT(*)` / `COUNT` / `SUM` / `AVG`
  / `MIN` / `MAX` (M10.10), `GROUP BY` + `HAVING` (M10.25), HAVING
  expression tree (M10.30), aggregate / `GROUP BY` inside `UNION`
  members (M10.36).
- **Set + shape ops.** `UNION` / `UNION ALL` (M10.26),
  `UNION` + projection (M10.27), `UNION` + `ORDER BY` (M10.28),
  `DISTINCT` (M10.31), `LIMIT` / `OFFSET` (M10.32),
  `BETWEEN` / `LIKE` (M10.33), multi-column `ORDER BY` (M10.37),
  `CASE WHEN` in projection (M10.38).
- **DML / DDL.** `INSERT` (M10.5), `ORDER BY` execution (M10.6),
  `UPDATE` / `DELETE` bulk through `AdsExecuteSQLDirect` (M10.7),
  projection lists (M10.8), DDL `CREATE TABLE` / `CREATE INDEX`
  (M10.9), VFP autoinc fields with persistent counter (M10.11),
  `AdsRestructureTable` CHANGE (same-type length / decimals)
  (M10.12), `INSERT INTO … SELECT` (M10.41), `CREATE TABLE AS
  SELECT` (M10.42).
- **Other SQL.** `WHERE` `OR` / `NOT` / parens + numeric literals
  (M10.3), scalar string fns (M10.39), arithmetic in projection
  (M10.40).
- **Engine + drivers.** Real `.add` Data Dictionary persistence —
  round-trips through `.add` reopen (M10.1); VFP I / Y / B field
  decode + encode (M10.2); branch-level MISS closure
  (`AdsRestructureTable` DELETE-fields + `AdsSetIndexDirection`)
  (M10.4).
- **AEP host (M11.4).** `CREATE PROCEDURE` + `EXECUTE PROCEDURE`
  through the OpenADS clean-room AEP runtime.
- **Encrypted DBFs (M11.2).** OpenADS-encrypted DBF — AES-256-CTR
  per-page, transparent through the read / write path.
- **VFP V / Q (M11.1).** Varchar + Varbinary field types.
- **Nested transactions (M11.3).** `AdsReleaseSavepoint` + nested
  `BEGIN` / `COMMIT` (proper save-point stack).

## 0.2.0 — 2026-05-04

The 0.2.0 release closes the entire 226-symbol Harbour-reachable
`Ads*` ABI surface — every export resolves to either a real
implementation or a documented local-mode silent-success. No exports
hard-fail with `AE_FUNCTION_NOT_AVAILABLE` at the function level any
more. The release also relicensed the project from MIT to Apache
License 2.0 and added a clean-room provenance / non-commercial /
no-warranty disclaimer block + NOTICE file.

### Highlights

- **Compound CDX expression evaluator.** `UPPER`, `LOWER`,
  `LTRIM` / `RTRIM` / `ALLTRIM`, `STR(n[,len[,dec]])`, `DTOS(date)`,
  `SUBSTR(s,start[,len])`, and string concatenation with `+`. UPPER /
  LOWER / SUBSTR walk UTF-8 codepoints (ASCII + Latin-1 supplement
  case map, `ÿ↔Ÿ` pair); the Latin-1 case mapping table closes the
  M9.17 `*W` Unicode surface.
- **Real CRUD for tables and indexes.** `AdsCreateTable` parses the
  rddads `NAME,Type,Len,Dec;…` field-def syntax; `AdsCreateIndex61` /
  `AdsCreateIndex` build CDX or NTX bags compatible with FoxPro and
  Clipper layouts. `AdsZapTable` / `AdsPackTable` / `AdsReindex`
  match the Clipper bound-index lifecycle.
- **Multi-file index binding.** Multiple `.ntx` files (or multiple
  pre-built `.cdx` bags) coexist on a single Table; same-path reopen
  refreshes; `AdsCloseIndex` drops the closed view without disturbing
  the active order.
- **Transactions + savepoints + WAL recovery.** `AdsBeginTransaction`
  / `AdsCommitTransaction` / `AdsRollbackTransaction` /
  `AdsCreateSavepoint` / `AdsRollbackTransaction80(savepoint)`. Mid-
  tx crash + reopen replays the WAL and writes back before-images
  for orphan transactions.
- **Memo (DBT / FPT) read + write + binary type.** Text memos
  round-trip through DBT and FPT; `AdsGetBinary` /
  `AdsGetBinaryLength` / `AdsSetBinary` carry binary blobs through
  FPT block-type tags (Text / Picture / Object); chunked
  `AdsSetBinary` writes reassemble through a per-(table, field)
  accumulator.
- **Locking with retry policy.** `AdsLockTable` / `AdsLockRecord`
  use non-blocking byte-range acquires (`LockFileEx
  LOCKFILE_FAIL_IMMEDIATELY` on Windows, `fcntl F_SETLK` on POSIX);
  `AdsSetLockCycle` / `AdsSetLockRetryCount` configure the retry
  budget.
- **Full-text search.** `AdsCreateFTSIndex` writes a clean-room
  `# OpenADS FTS v0` text file per table; `AdsFTSSearch` and the
  SQL `CONTAINS(<col>, '<query>')` predicate intersect per-token
  recno lists with AND semantics.
- **Server / dictionary surface.** `AdsMg*` (15 calls) report local-
  mode "everything quiescent" responses; `AdsDD*` (14 advanced-DD
  calls) accept silently and zero-fill property getters. Real
  persistence in the OpenADS DD format lands with 0.3.x.
- **Schema evolution.** `AdsRestructureTable` ADD-fields path
  rewrites the DBF with extended schema and preserves every
  record's original-field bytes; DELETE / CHANGE arguments still
  surface AE_FUNCTION_NOT_AVAILABLE pending VFP / ADT structural
  extensions.
- **Misc.** Real `AdsGetServerName` / `AdsGetServerTime`,
  `AdsGetLongLong`, `AdsSetFieldRaw`, `AdsVerifySQL`,
  `AdsFailedTransactionRecovery`, `AdsGetAllLocks`, `AdsSkipUnique`,
  `AdsFindFirstTable` / `AdsFindNextTable` / `AdsFindClose`,
  `AdsCopyTable` / `AdsCopyTableContents` / `AdsConvertTable`,
  `AdsAddCustomKey` / `AdsDeleteCustomKey`.

### Project posture

- License: relicensed **MIT → Apache License 2.0** (`LICENSE` +
  `NOTICE`).
- Independence + non-commercial purpose + clean-room provenance +
  no-warranty + downstream responsibility — block added to the
  README and mirrored to the NOTICE file (Apache 4(d) preservation).
- Tests: **214** doctest cases, **3865** assertions, all green on
  Windows / MSVC Release. CI matrix builds Windows + Linux + macOS
  cleanly through `.github/workflows/ci.yml`.

### Milestones

| Tag | Milestone |
|-----|-----------|
| `m9.1`   | Compound CDX expression evaluator |
| `m9.2`   | Stub batch reorganised into real / no-op / missing |
| `m9.3`   | Compound expressions validated through Harbour |
| `m9.4`   | `AdsGotoRecord` + table / file metadata |
| `m9.5`   | `AdsCreateTable` |
| `m9.6`   | `AdsRefreshRecord` + `AdsExtractKey` |
| `m9.7`   | `AdsCreateIndex61` with compound expression |
| `m9.8`   | `AdsZapTable` + `AdsPackTable` |
| `m9.9`   | `AdsReindex` |
| `m9.10`  | NTX multi-level B+tree split |
| `m9.11`  | `AdsCopyTable` / `AdsCopyTableContents` / `AdsConvertTable` |
| `m9.12`  | `AdsFindFirstTable` / `AdsFindNextTable` / `AdsFindClose` |
| `m9.13`  | Binary memo (`AdsGetBinary` / `AdsSetBinary` / `AdsGetBinaryLength`) |
| `m9.14`  | NTX multi-tag binding |
| `m9.15`  | Real `AdsGetServerName` / `AdsGetServerTime` + binding-leak fix |
| `m9.16`  | Chunked `AdsSetBinary` |
| `m9.17`  | Unicode `*W` variants |
| `m9.18`  | Lock retry / cycle policy |
| `m9.19`  | `AdsCreateFTSIndex` |
| `m9.20`  | `AdsAddCustomKey` / `AdsDeleteCustomKey` |
| `m9.21`  | FTS search side (`AdsFTSSearch` + SQL `CONTAINS`) |
| `m9.22`  | UTF-8 codepoint-aware index-expression evaluator |
| `m9.23`  | Misc MISS fillers (LongLong / FieldRaw / VerifySQL / FailedTxRecovery / GetAllLocks / SkipUnique) |
| `m9.24`  | Local-mode `AdsMg*` surface (15 calls) |
| `m9.25`  | Local-mode `AdsDD*` CRUD surface (14 calls) |
| `m9.26`  | `AdsRestructureTable` (ADD-fields path) |
| `m9.27`  | CI matrix portability |

## 0.1.0 — 2026-05-04

Final 0.1.0. The post-rc1 work below extends the Harbour smoke
beyond the read path covered in 0.1.0-rc1: a real Harbour app now
also drives multi-tag focus swaps, ARIES-style transactions, and
memo M-field round-trips end-to-end through OpenADS' `ace64.dll`.

### M8.9 — Multi-tag CDX + OrdSetFocus

- `AdsOpenIndex` widened to its real 4-arg signature
  `(hTable, pucName, ahIndex[], &pu16ArrayLen)`. Every tag inside a
  compound CDX is opened by name through `CdxIndex::open_named`;
  the first tag's IIndex moves into `Table::set_order` and the rest
  park in their bindings.
- `Table::take_order()` / `Order::release()` surrender the active
  index's `unique_ptr<IIndex>` so a focus swap can park it in the
  previous binding's slot.
- `get_table` and `table_for_index` now call `activate_binding(h)`
  whenever a navigation call arrives with an index handle, so
  rddads' `pArea->hOrdCurrent` swaps drive the Table's active order
  in lockstep.
- `AdsGetIndexHandle` strips trailing whitespace from the caller's
  tag name; `AdsGetIndexName` / `AdsGetIndexExpr` read each
  binding's metadata directly so parked tags report their real name
  even before they become live.
- `AdsGetNumIndexes` returns the per-table binding count.

### M8.10 — Transactions through Harbour

- A real Harbour app drives `AdsBeginTransaction` /
  `AdsRollback` / `AdsCommitTransaction` directly. BEGIN + APPEND +
  ROLLBACK leaves the appended row in the DBF flagged deleted (CDX
  index entries persist by design — `Found()` still reports `T` but
  `Deleted()` is `T`); BEGIN + APPEND + COMMIT persists durably to
  both the DBF and every CDX tag.
- `Table::register_extra_index_view` /
  `Table::unregister_extra_index_view` /
  `Table::clear_extra_index_views` track the parked CDX sub-tags as
  non-owning views; the binding still owns the IIndex lifetime.
- `Table::snapshot_index_keys_()` captures the pre-write key per
  index — active order plus extras — and `sync_all_indexes_(snap)`
  erases each prior `(recno, prev_key)` and inserts the new one in
  lockstep, so a `set_field` on a multi-tag CDX keeps every tag
  consistent (M8.8 only synced the active order).
- `Table::flush()` flushes the active order **and** every extra
  view so a multi-tag commit reaches disk for every tag.

### M8.11 — Memo M-fields (FPT)

- A real Harbour app appends rows whose `FIELD->NOTES` carries a
  short memo (43 bytes) and a longer memo (280 bytes), closes the
  area, reopens, and reads the memos back via the standard Clipper
  RDD surface.
- `make_cdx.exe` now also writes an empty `data.fpt` next to
  `data.cdx` via `FptMemo::create`, so `Connection::open_table` finds
  a memo store to auto-attach when the DBF declares an M field.
- `AdsGetMemoLength` / `AdsGetMemoDataType` / `AdsGetString` are now
  real implementations using `resolve_field_index` (M4 had earlier
  versions that only accepted string field names; rddads passes the
  `ADSFIELD(n)` integer form).
- `AdsCloseTable` flushes the table before releasing the handle so
  non-transactional appends reach disk on `USE` close.
- `ADS_MEMO_TEXT` / `ADS_MEMO_PICTURE` aliases resolve to the
  M8.4-verified `ADS_STRING` (4) and `ADS_IMAGE` (7) values.

## 0.1.0-rc1 — 2026-05-03

First end-to-end validation against Harbour `contrib/rddads`. A real
`.prg` compiled with `hbmk2 -comp=msvc64 -lrddads -lace64` opens a
DBF, walks records, runs `dbSeek`, appends rows, and reopens — every
call lands on OpenADS' `ace64.dll` with no Harbour rebuild.

### M0–M3.10 (engine + drivers)

- 5-layer architecture: ABI shim → Session/Connection → SQL → Engine
  (Table / Index / MemoStore / LockMgr / TxLog) → OS abstraction.
- DBF read/write for C / N / L / D columns; deletion flag; flush.
- CDX driver with compound layout (file header + structure-tag root +
  per-tag CDXTAGHEADER + sub-tag B+tree). Multi-tag-per-file API
  (`add_tag` / `open_named` / `list_tags`). FoxPro-equivalent leaf
  bit-pack (mirrors Harbour `hb_cdxPageLeafInitSpace`). Compound CDX
  closes the last reviewer-flagged compat-breaking item.
- NTX driver with cache-based in-order traversal for multi-level
  trees; leaf-split fix promotes the separator without duplicating it.
- AES-128 / AES-256 ECB primitives (vendored tiny-AES-c, Unlicense),
  validated against FIPS-197 + NIST SP 800-38A.
- DBT + FPT memo round-trip.
- Data Dictionary (`.add`): `TABLE alias=path` text format with alias
  resolution.
- Minimal SQL: `SELECT * FROM <table> [WHERE col op 'lit' [AND ...]]`
  with six comparison operators.

### M4 — Locking

- OS byte-range locking with ranges compatible with original ACE so
  installs can coexist during migration.

### M5 — Transactions

- WAL with CRC-32C records (BEGIN / UPDATE / COMMIT / ABORT).
- In-memory ordered op log with named savepoints (M5.3).
- Group commit (M5.4): each record carries a monotonic LSN;
  `sync_to(lsn)` is the group-commit primitive — first thread to
  observe `last_synced_lsn_ < lsn` issues a single fsync covering
  the high-water mark.
- Idempotent recovery (M5.5) via `openads.lsnmap` sidecar.
  Concurrent recovery passes can never regress the per-record
  watermark.

### M6 — Data Dictionary

- `.add` parser, `Connection::open(<path>.add)` resolves member
  tables through the dictionary on every `AdsOpenTable`.

### M7 — SQL

- Parser + executor for `SELECT *` + multi-clause `WHERE` joined by
  implicit `AND`. Compiles to a `Table::RowPredicate` closure used by
  `AdsExecuteSQLDirect` to filter the cursor's record stream.

### M8 — Harbour conformance (this release)

- **M8.0** `ace64.dll` / `ace32.dll` SHARED CMake target with a `.def`
  exporting 80 real `Ads*` entry points.
- **M8.1** 226 `Ads*` exports — superset of every symbol Harbour
  `rddads.lib` references; the 146 newly-stubbed entries return
  `AE_FUNCTION_NOT_AVAILABLE` (5004) so the link resolves cleanly.
- **M8.2** Six legacy MSVC2013-era CRT shims (`_dclass`, `_dsign`,
  `_wfsopen`, `_getch`, `_kbhit`, `_eof`) re-exported under aliases
  so Harbour's prebuilt msvc64 libs link against modern UCRT.
- **M8.2** `smoke.exe` runs end-to-end: `AdsVersion()` resolves
  through the rddads wrapper to OpenADS' `AdsGetVersion`.
- **M8.3** `USE data VIA "ADSCDX"` + walk records. `Connection::open_table`
  auto-appends `.dbf`. `AdsGetField` / `AdsGetFieldType` /
  `AdsGetFieldLength` accept either string field names or the
  `ADSFIELD(n)` integer-cast-as-pointer form. `AdsConnect`
  is now a real wrapper around `AdsConnect60`.
- **M8.4** ACE field-type constants verified by sweeping
  `AdsGetFieldType`'s return through 0..40 against rddads. Result:
  `ADS_LOGICAL = 1`, `ADS_NUMERIC = 2`, `ADS_DATE = 3`,
  `ADS_STRING = 4`, ... — the inverse of the public ACE SDK ordering
  in some places. Mapping captured in `include/openads/ace.h`.
- **M8.5** Multi-field DBF (C/N/L/D) end-to-end. `AdsGetFieldDecimals`,
  `AdsGetLong`, `AdsGetDouble`, `AdsGetJulian` real impls (was 5004
  stubs); `AdsGetJulian` parses `YYYYMMDD` and computes Clipper Julian
  Day Numbers using the same Gregorian formula as `hb_dateEncode`.
- **M8.6** `dbSeek` end-to-end through OpenADS' CDX. `Table::path()`,
  index path resolution + auto-extension, polymorphic `get_table`
  (accepts table or index handles — rddads' `adsGoTop` calls
  `AdsGotoTop(hOrdCurrent)` when an order is active), 6-arg `AdsSeek`
  signature matching rddads, real `AdsIsFound` reading
  `Table::last_seek_found_`.
- **M8.7** Write path: `dbAppend` + `FIELD-> := value` + `dbCommit` +
  reopen. `AdsSetString` / `AdsSetLogical` / `AdsSetDouble` /
  `AdsSetLongLong` / `AdsSetJulian` real impls; field index resolution
  via `resolve_field_index`.
- **M8.8** Active index auto-syncs on every record mutation.
  `Table::compute_index_key_` evaluates bare-field-name expressions
  against the current `record_buf_`; `Table::sync_active_index_`
  erases the prior `(recno, prev_key)` and inserts the new one.
  `Table::flush()` flushes both the driver and the index.

### Tests

- 135 doctest cases / 1820 assertions passing on Windows / MSVC
  Release.
- One `tests/harbour_smoke/` integration harness producing a
  runnable `smoke.exe` that exercises the full Harbour →
  rddads.lib → OpenADS path.
