# OpenADS

A free and open-source implementation compatible with Advantage Database Server (ADS), discontinued by SAP.

The goal is to provide a *drop-in* replacement for the Advantage Client Engine (`ace32.dll` / `ace64.dll` / `libace.so`) so existing applications — particularly Harbour/Clipper apps using `contrib/rddads` — keep working without recompilation.

## Status

Phase 1 in design. No code yet.

## Phase 1 scope

| Topic | Decision |
|------|----------|
| Operation mode | LOCAL only (no remote server). Phase 2 will add a TCP server reusing the same engine. |
| Table formats | ADT + CDX + NTX + VFP (all four ADS-supported types). |
| Memo / index | ADM / FPT / DBT (memo) · ADI / CDX / NTX (index). |
| ABI compatibility | Identical C ABI to ACE; applications do not recompile. |
| Validation frontend | `c:\harbour\contrib\rddads`, unmodified. |
| SQL | Full Advantage SQL dialect (parser + planner + executor + xBase UDFs + AEP host for external stored procedures). |
| Concurrency | OS *byte-range* locking with ranges identical to ACE — coexistence with original ACE installations during migration. |
| Data Dictionary (`.add`) | Full support: member tables, users/groups/permissions, RI, views, procedures, links, validations, defaults. |
| Encryption | AES-128 / AES-256 (ADS 9+ format). The legacy proprietary cipher is out of scope for phase 1. |
| Transactions (TPS) | Multi-table ACID, savepoints, crash recovery via write-ahead log. |
| Platforms | Windows (x86 + x64), Linux, macOS, BSD. |
| Language / build | C++17 with `extern "C"` external ABI. CMake + GitHub Actions. |
| i18n | OEM ↔ ANSI ↔ UTF-8 ↔ UTF-16 (the API's `*W` variants). |
| License | MIT. |

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  Harbour application (no recompilation)                          │
│    REQUEST ADS / dbUseArea( .T., "ADS", ... )                    │
└────────────────────────┬─────────────────────────────────────────┘
                         │  Clipper RDD API
┌────────────────────────▼─────────────────────────────────────────┐
│  rddads.lib  (contrib/rddads — untouched)                        │
│    ads1.c / adsfunc.c / adsx.c / adsmgmnt.c                      │
└────────────────────────┬─────────────────────────────────────────┘
                         │  ACE C ABI (~230 Ads* entry points)
                         │  ace.h headers
═════════════════════════╪══════════════════════════════════════════
                         │       ▼  OPENADS REPLACES HERE
┌────────────────────────▼─────────────────────────────────────────┐
│  L1 — ABI Layer  (libace32.dll / libace64.dll / libace.so)       │
│    extern "C" wrappers → ACE handle translation → C++ engine     │
│    Error code mapping (ACE codes ↔ engine errors)                │
│    OEM / ANSI / UTF-8 / UTF-16 translation                       │
└────────────────────────┬─────────────────────────────────────────┘
                         │  internal C++ API (RAII handles)
┌────────────────────────▼─────────────────────────────────────────┐
│  L2 — Session / Connection Layer                                 │
│    Connection (local path or Data Dictionary)                    │
│    Statement (prepared SQL cursor)                               │
│    HandleRegistry (ADSHANDLE → object pointer, thread-safe)      │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│  L3 — SQL Engine                                                 │
│    Lexer → Parser (AST) → Resolver → Planner → Executor          │
│    DD-aware Catalog, xBase UDFs                                  │
│    AEP host (stored procedures as .dll/.so plugins)              │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│  L4 — Engine Core (transport-agnostic)                           │
│    Table / Index / MemoStore / Cursor / Filter (AOF)             │
│    LockMgr (OS byte-range, ACE-compatible ranges)                │
│    TxLog (multi-table WAL ACID + savepoints + crash recovery)    │
│    Catalog (DD .add reader/writer)                               │
│    PageCache / BufferMgr                                         │
└────────────────────────┬─────────────────────────────────────────┘
                         │  Driver trait (open / read / write page)
        ┌────────────────┼────────────────┬───────────────┐
        ▼                ▼                ▼               ▼
   ┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐
   │AdtDriver│     │CdxDriver│     │NtxDriver│     │VfpDriver│
   │.adt+.adm│     │.dbf+.cdx│     │.dbf+.ntx│     │.dbf+.fpt│
   │   +.adi │     │   +.fpt │     │   +.dbt │     │   +.cdx │
   └────┬────┘     └────┬────┘     └────┬────┘     └────┬────┘
        └────────────────┴────────────────┴────────────────┘
                                 │
┌────────────────────────────────▼─────────────────────────────────┐
│  L5 — OS Abstraction (Platform)                                  │
│    File I/O · mmap · byte-range locks · paths · time · threads   │
│    Win32 and POSIX implementations                               │
└──────────────────────────────────────────────────────────────────┘
```

### Key boundaries

- **L1** is the only module with C ABI. Everything else is internal C++17.
- L4's **driver trait** is the extension point: each table format is a swappable driver.
- The **remote server** in phase 2 will simply be another L1 frontend (TCP transport layer); L2 through L5 are reused as is.
- The **SQL engine** (L3) consumes L4 only through `Cursor` and `Catalog`; it has no knowledge of file formats.
- The **LockMgr** preserves the exact byte-range semantics of ACE, allowing coexistence with original ACE installations on the same files during migration.

## Repository layout

```
OpenADS/
├── CMakeLists.txt              # top-level build, presets per platform
├── CMakePresets.json
├── LICENSE                     # MIT
├── README.md
├── docs/
│   ├── architecture.md
│   ├── ace-coverage.md         # entry-point status table (~230 fns)
│   ├── adt-format.md           # ADT/ADM/ADI on-disk spec
│   ├── lock-ranges.md          # ACE-compat byte-range table
│   ├── tx-log.md               # WAL format + recovery protocol
│   └── sql-grammar.md          # Advantage SQL EBNF + diffs
│
├── third_party/                # vendored deps
│   ├── tinyaes/                # AES-128/256 (MIT)
│   ├── utf8.h/                 # UTF conversion (MIT)
│   ├── doctest/                # unit test framework (MIT)
│   └── ace-headers/            # ace.h, adscd.h (Sybase, redistribution OK)
│
├── include/openads/            # public C++ headers (consumed by L1)
│   ├── engine.h
│   ├── connection.h
│   ├── table.h
│   ├── cursor.h
│   ├── catalog.h
│   └── error.h
│
├── src/
│   ├── abi/                    # L1 — ACE C ABI shim
│   │   ├── ace_exports.def     # Windows DLL export list
│   │   ├── ace_exports.cpp     # ~230 extern "C" thunks
│   │   ├── handle_registry.cpp # ADSHANDLE ↔ object map
│   │   ├── error_map.cpp       # ACE error codes
│   │   ├── charset.cpp         # OEM/ANSI/UTF conversion
│   │   └── version.cpp         # AdsGetVersion / AdsGetServerName
│   │
│   ├── session/                # L2
│   │   ├── connection.cpp
│   │   ├── statement.cpp
│   │   └── globals.cpp         # AdsSetDefault / AdsSetFileType / etc.
│   │
│   ├── sql/                    # L3
│   │   ├── lex/lexer.cpp
│   │   ├── parse/parser.cpp        # recursive-descent
│   │   ├── parse/ast.h
│   │   ├── resolve/resolver.cpp    # name binding, type check
│   │   ├── plan/planner.cpp        # logical → physical
│   │   ├── plan/optimizer.cpp      # predicate pushdown, index selection
│   │   ├── exec/executor.cpp       # iterator pipeline
│   │   ├── exec/operators/         # scan / filter / sort / agg / join / ...
│   │   ├── func/scalar.cpp         # xBase UDFs (LEFT/SUBSTR/CTOD/...)
│   │   ├── func/aggregate.cpp
│   │   └── aep/host.cpp            # AEP plugin loader (.dll / .so)
│   │
│   ├── engine/                 # L4 — core
│   │   ├── table.cpp
│   │   ├── cursor.cpp
│   │   ├── filter_aof.cpp      # Advantage Optimized Filter
│   │   ├── lock_mgr.cpp        # OS byte-range, ACE-compat ranges
│   │   ├── tx_log.cpp          # WAL writer
│   │   ├── tx_recover.cpp      # crash recovery
│   │   ├── savepoint.cpp
│   │   ├── catalog_dd.cpp      # .add reader / writer
│   │   ├── page_cache.cpp
│   │   ├── buffer_mgr.cpp
│   │   └── encryption.cpp      # AES-128 / 256
│   │
│   ├── drivers/                # L4 — format drivers
│   │   ├── driver_trait.h      # abstract interface
│   │   ├── adt/
│   │   │   ├── adt_table.cpp   # .adt header + records
│   │   │   ├── adi_index.cpp   # .adi B+tree
│   │   │   ├── adm_memo.cpp    # .adm blob store
│   │   │   └── field_types.cpp # autoinc / GUID / modtime / timestamp / ...
│   │   ├── cdx/
│   │   │   ├── dbf_table.cpp
│   │   │   ├── cdx_index.cpp   # FoxPro CDX compact index
│   │   │   └── fpt_memo.cpp
│   │   ├── ntx/
│   │   │   ├── dbf_table.cpp   # shared with cdx via dbf_common
│   │   │   ├── ntx_index.cpp   # Clipper NTX
│   │   │   └── dbt_memo.cpp
│   │   ├── vfp/
│   │   │   ├── vfp_table.cpp   # DBF v0x30 + nullable + autoinc
│   │   │   ├── cdx_index.cpp   # symlink to ../cdx
│   │   │   └── fpt_memo.cpp
│   │   └── dbf_common.cpp      # shared DBF header logic
│   │
│   ├── platform/               # L5 — OS abstraction
│   │   ├── file.h
│   │   ├── file_win32.cpp
│   │   ├── file_posix.cpp
│   │   ├── lock.h
│   │   ├── lock_win32.cpp      # LockFileEx
│   │   ├── lock_posix.cpp      # fcntl F_SETLK
│   │   ├── mmap.cpp
│   │   ├── path.cpp            # case-insensitive matching on unix
│   │   ├── time.cpp
│   │   └── thread.cpp
│   │
│   └── util/
│       ├── span.h
│       ├── result.h            # error-or-value
│       └── log.cpp
│
├── tests/
│   ├── unit/                   # doctest, per-module
│   │   ├── adt_table_test.cpp
│   │   ├── cdx_index_test.cpp
│   │   ├── lock_mgr_test.cpp
│   │   ├── tx_log_test.cpp
│   │   ├── sql_parser_test.cpp
│   │   └── ...
│   ├── integration/            # full ABI roundtrip
│   │   ├── harbour_smoke.prg   # runs against rddads
│   │   ├── byte_compat/        # diff vs reference ACE-produced files
│   │   └── conformance/        # ACE entry-point matrix
│   └── fixtures/               # canonical .adt / .dbf / .cdx samples
│
├── tools/
│   ├── adt-dump/               # CLI: hex-dump ADT structure
│   ├── cdx-dump/
│   ├── tx-replay/              # WAL replay / inspect
│   └── ace-trace/              # log every ACE call (debug shim)
│
├── benchmarks/
│   └── micro/                  # paged read, lock contention, SQL
│
└── .github/workflows/
    ├── ci.yml                  # matrix Win / Linux / macOS / BSD
    ├── compat.yml              # nightly run vs Harbour rddads tests
    └── release.yml             # tagged DLL / SO artifacts
```

### Module notes

- **`src/abi/ace_exports.cpp`** is the only contact point with the C ABI. A `static constexpr` table maps each ACE entry point to its internal handler. Optionally generated by a script from `ace.h`.
- **`src/drivers/driver_trait.h`** defines the minimum interface: `open / close / read_record / write_record / seek / scan / index_create / index_seek / lock_range`. Each driver is roughly 3–5 files.
- **`src/engine/lock_mgr.cpp`** centralises lock ranges — the single source of truth for ACE coexistence.
- **`src/engine/tx_log.cpp`** and **`tx_recover.cpp`** are driver-independent: the WAL log records `(driver_id, page_no, before_image, after_image)` as opaque payloads.
- **`src/sql/`** is driver-independent and operates against an abstract `Cursor`. SQL tests do not require real drivers (mock cursor).
- **`src/platform/`** is the only OS dependency. Engine tests use a platform mock to inject I/O failures.
- **`tools/`** is the debugging artillery — critical for byte-level diffs against original ACE.

## Data flow

### Example A — Opening a CDX table from Harbour

```
Harbour PRG
  USE clientes VIA "ADSCDX" SHARED
       │
       ▼
rddads.lib :: hb_adsOpen()                 [contrib/rddads/ads1.c]
  AdsConnect60( "C:\data", ..., &hConn )   ← OpenADS L1 entry
  AdsOpenTable( hConn, "clientes.dbf",     ← OpenADS L1 entry
                ADS_CDX, ADS_DEFAULT,
                ADS_NONE, ADS_DEFAULT,
                ADS_DEFAULT, &hTbl )
       │
       ▼  L1 ace_exports.cpp
extern "C" AdsConnect60(...)
  → openads::Connection::open( path, ... )
       │
       ▼  L2 session/connection.cpp
Connection ctor
  → resolves path, registers handle, returns ADSHANDLE via HandleRegistry
       │
       ▼  back to L1
extern "C" AdsOpenTable(...)
  → conn->open_table( "clientes.dbf", FormatHint::Cdx, OpenMode::Shared )
       │
       ▼  L2 → L4 engine/table.cpp
Table::open()
  1. DriverFactory::detect_or_force(path, hint) → CdxDriver
  2. CdxDriver::open()
       ├─ platform::file_open("clientes.dbf", RW)
       ├─ read DBF header (32 bytes + field descriptors)
       ├─ platform::file_open("clientes.cdx", RW)   if it exists
       ├─ CdxIndex::load_root_pages()
       └─ FptMemo::open("clientes.fpt")              if memo fields
  3. LockMgr::acquire_open_share()  (byte-range header lock, ACE range)
  4. PageCache::register(file_handles)
  5. TxLog::register_table(table_id) (no-op outside a transaction)
  6. returns Table*
       │
       ▼
HandleRegistry::register(Table*) → ADSHANDLE
       │
       ▼
return SUCCESS to rddads → returned to PRG
```

### Example B — `SELECT * FROM clientes WHERE saldo > 1000 ORDER BY nombre`

```
PRG
  AdsCreateSQLStatement( hConn, &hStmt )
  AdsExecuteSQLDirect( hStmt, "SELECT...", &hCursor )
       │
       ▼  L1
extern "C" AdsExecuteSQLDirect(hStmt, sql, &cursor)
  → stmt->execute_direct(sql)
       │
       ▼  L2 session/statement.cpp → L3
Statement::execute_direct(sql)
  ┌──────────── L3 sql/ pipeline ───────────────────────────┐
  │ Lexer    → tokens                                        │
  │ Parser   → AST (SelectStmt)                              │
  │ Resolver → bind "clientes" via Catalog → Table*          │
  │            bind columns saldo, nombre → ColumnRef        │
  │            type-check predicates                         │
  │ Planner  → LogicalPlan:                                  │
  │              Sort(nombre)                                │
  │                └─ Filter(saldo > 1000)                   │
  │                     └─ Scan(clientes)                    │
  │ Optimizer → predicate pushdown, index selection:         │
  │              if index on (saldo) → IndexRangeScan        │
  │              else                  → SeqScan + Filter    │
  │              if index on (nombre)  → drop Sort           │
  │ Executor → builds operator tree (iterator pipeline):     │
  │              SortOp                                      │
  │                └─ FilterOp                               │
  │                     └─ TableScanOp(Cursor over Table)    │
  └──────────────────────────────────────────────────────────┘
       │
       ▼  Cursor handed back
HandleRegistry::register(Cursor*) → ADSHANDLE returned as hCursor
       │
       ▼
PRG fetches rows via AdsGotoTop / AdsGetRecord / AdsSkip
  → each call drives one Executor::next() through L4 PageCache
  → AdsGetField → field decode (xBase types or ADT extended types,
                  depending on driver)
```

### Example C — Multi-table transaction

```
AdsBeginTransaction(hConn)
  → TxLog::begin(tx_id)         (write BEGIN record)
  → LockMgr::tx_attach(tx_id)

AdsLockRecord(hTblA, 42)
  → LockMgr::lock_record(tx_id, A, 42)   (escalates byte-range lock)

AdsUpdateRecord(hTblA, ...)
  → Table::update_record():
       1. Capture before-image of pages dirtied
       2. Apply change in PageCache
       3. TxLog::log_update(tx_id, A, page_no, before, after)

AdsAppendRecord(hTblB, ...)
  → similar, writes a log record for table B

AdsCommitTransaction(hConn)
  → TxLog::commit(tx_id)        (write COMMIT, fsync log)
  → PageCache::flush_tx_pages(tx_id)
  → LockMgr::tx_release(tx_id)

AdsRollbackTransaction(hConn)  [alternative]
  → TxLog::rollback_walk(tx_id):
       reverse-iterate log, restore before-images via PageCache
  → LockMgr::tx_release(tx_id)
```

#### Crash recovery (next process startup)

```
TxRecover::run()
  scan TxLog tail
  for each tx without COMMIT          → roll back (apply before-images)
  for each tx with COMMIT but pages unflushed → roll forward
  truncate log
```

## Concurrency and lock ranges

### Locking modes

`AdsLocking(ON | OFF)` selects the byte-range scheme:

| Mode | When to use | Coexistence |
|------|-------------|-------------|
| **Proprietary** (default) | ADS-only deployments | ADS-specific ranges, fastest |
| **Compatible** (`ADS_COMPATIBLE_LOCKING`) | Coexistence with Clipper / FoxPro / Harbour `DBF*` RDDs over the same files | Standard Clipper / FoxPro ranges |

OpenADS supports both modes. The `rddads` frontend selects via `AdsSetServerType(ADS_LOCAL_SERVER)` combined with `AdsLocking(ON | OFF)`.

### Granularity

Three hierarchical levels:

1. **File lock** (exclusive) — locks the whole table. `AdsLockTable` / `flock` open mode `EXCLUSIVE`.
2. **Record lock** — shared for reads, exclusive for writes. `AdsLockRecord(recno)`.
3. **Header lock** — first byte of the file header, taken only during writes that mutate the header (append, pack, zap, recno recalculation).

Rules:

- Open SHARED → acquires a shared header lock over the header range.
- Append / Pack / Zap → exclusive header lock plus an exclusive file-equivalent lock (Compatible mode uses byte `LOCKOFFSET_FILE`; Proprietary uses an internal ADS range).
- Update record → record lock required (RDD enforcement, not OS).
- Read record → no lock required in `READ COMMITTED`; `READ UNCOMMITTED` skips even versioning.

### Concrete byte-range layout

#### Compatible mode (Clipper / FoxPro coexistence)

```
DBF + NTX (Clipper scheme):
  FILE LOCK    : offset 1_000_000_000        size 1
  RECORD LOCK  : offset 1_000_000_001 + recno size 1

DBF + CDX (FoxPro scheme — descending):
  FILE LOCK    : offset 0x7FFFFFFE           size 1
  RECORD LOCK  : offset 0x7FFFFFFE - recno   size 1

DBF + VFP CDX (FoxPro VFP — same as CDX but offset 0x40000000-1):
  FILE LOCK    : offset 0x3FFFFFFE           size 1
  RECORD LOCK  : offset 0x3FFFFFFE - recno   size 1

ADT proprietary:
  FILE LOCK    : offset 0x80000000_00000000  size 0x10000  (64-bit)
  RECORD LOCK  : offset 0x80000000_00000000 + (recno << 16)  size 1
```

#### Proprietary mode

ADS-specific. The ranges are derived from captures of original ACE running over an instrumented filesystem and pinned in `docs/lock-ranges.md`. For phase 1 they are a constant table, validated by `tools/ace-trace`.

### LockMgr API (L4)

```cpp
class LockMgr {
public:
    // OS-level byte-range — inter-process coexistence
    Result<LockToken> lock_file_excl(FileHandle& fh, LockingMode mode);
    Result<LockToken> lock_record   (FileHandle& fh, LockingMode mode,
                                     uint64_t recno, LockType, Timeout);
    Result<>          unlock        (LockToken);

    // Tx-scoped — lifetime tied to TxLog::tx_id
    Result<>          lock_for_tx   (tx_id_t, FileHandle& fh,
                                     uint64_t recno, LockType);
    void              release_tx    (tx_id_t);

    // Intra-process re-entry
    bool              already_held  (FileHandle& fh, uint64_t recno) const;
};
```

Notes:

- **Intra-process re-entrancy.** A connection that already holds a record lock does not call the OS again; only a local counter is incremented.
- **Timeouts.** Per-connection (`AdsSetWaitTime`-equivalent), default 0 (fail fast).
- **Deadlock detection.** Cycle search over the `tx_id → resource → tx_id` graph. On detection, the youngest transaction is aborted (ADS does not document this precisely; OpenADS picks the youngest as victim).
- **Errors.** `AE_LOCKED` (5012) and `AE_LOCK_FAILED` (5013), mapped from LockMgr return codes.

### Coordination with TxLog

Inside an `AdsBeginTransaction` block, locks become tx-scoped: they are not released by `AdsUnlockRecord`, only by `AdsCommitTransaction` / `AdsRollbackTransaction`. This is required to preserve isolation guarantees.

Outside any transaction (autocommit), unlock releases immediately.

### Critical tests

- `lock_mgr_test.cpp` — re-entrancy, timeouts, deadlock detection.
- `tools/ace-trace` running real applications against original ACE → captures range logs → diffs against OpenADS.
- Multi-process conformance test: two OpenADS processes plus one original-ACE process operating on the same CDX table in Compatible mode — must complete without corruption.

## Transactions and write-ahead log

### Model

ARIES-lite. Page-level physiological logging. Multi-table atomicity. Nestable savepoints. Deterministic crash recovery.

### Log location

A single log shared per **data directory** (or per Data Dictionary if an `.add` file exists):

```
<data-dir>/openads.txlog          ← active log
<data-dir>/openads.txlog.<n>      ← rotated archives (truncated post-checkpoint)
<data-dir>/openads.checkpoint     ← latest CP record (LSN, dirty page table)
```

One log per DD avoids cross-DD coordination. Applications that do not use a DD but open tables in the same directory automatically share the log (detected by `Connection::open`).

### Record format

```
struct LogRecord {
    uint64_t  lsn;            // monotonic, unique
    uint64_t  prev_lsn;       // previous record in this tx (chain for undo)
    uint64_t  tx_id;
    uint16_t  type;           // BEGIN | UPDATE | INSERT | DELETE | ...
    uint16_t  driver_id;      // adt | cdx | ntx | vfp | memo | index
    uint32_t  table_id;       // assigned at first touch by tx
    uint64_t  page_no;
    uint16_t  before_len;
    uint16_t  after_len;
    uint8_t   payload[];      // before_image || after_image
    uint32_t  crc32c;         // record integrity
};
```

Record types:

| Type | Semantics |
|------|-----------|
| `BEGIN` | tx started, includes timestamp |
| `UPDATE` | physiological page update (before + after image) |
| `INSERT` | new record append (after only; undo = decrement recno) |
| `DELETE` | logical delete (after only; undo = clear deleted flag) |
| `INDEX_OP` | B+tree mutation (page split / merge / insert / delete key) |
| `MEMO_ALLOC` / `MEMO_FREE` | blob lifecycle in `.adm` / `.fpt` / `.dbt` |
| `SAVEPOINT` | named marker; `prev_lsn` chains here on partial rollback |
| `CLR` | compensation log record (written during undo, redo-only) |
| `COMMIT` | tx end ok, fsync barrier |
| `ABORT` | tx end rollback; all CLRs already written |
| `CHECKPOINT_BEGIN` / `CHECKPOINT_END` | stable point, dirty page table snapshot |

CLRs (compensation log records) make undo idempotent: a crash during rollback simply re-executes the CLRs without duplicating work (CLRs are redo-only, never undo).

### Group commit

```
AdsCommitTransaction(hConn)
  → TxLog::append(COMMIT_PENDING)              [in memory]
  → TxLog::group_commit_barrier()              [waits up to 10ms or N tx]
       └─ writev() batched COMMIT records
       └─ fsync(log_fd)
  → tx becomes visible
```

10 ms / 64 tx, whichever first (configurable). Reduces `fsync × N` to a single `fsync`.

### Savepoints

```
AdsCreateSavepoint(hConn, "sp1")
  → TxLog::append(SAVEPOINT name=sp1 lsn=L1)
  → conn->savepoint_stack.push("sp1", L1)

AdsRollbackTransaction80(hConn, "sp1")    // partial
  → walk back log from current_lsn to L1, write CLRs for each record
  → discard savepoints above sp1
  // tx still active
```

The classic `AdsRollbackTransaction()` performs a full rollback to BEGIN.

### Recovery (startup)

Three ARIES-lite phases:

```
TxRecover::run() {
  // 1. ANALYSIS
  scan log forward from the last CHECKPOINT_BEGIN
  build active_tx_table  (tx_id → last_lsn)
  build dirty_page_table (page_id → first_lsn that dirtied it)

  // 2. REDO
  scan log forward from min(dirty_page_table.first_lsn)
  for each UPDATE / INDEX_OP / INSERT / DELETE / CLR:
      if page.lsn < record.lsn:                 // not yet applied
          apply after_image to page
          page.lsn = record.lsn

  // 3. UNDO (loser txs only — no COMMIT seen)
  for each tx in active_tx_table sorted by last_lsn DESC:
      walk prev_lsn chain
      for each non-CLR record:
          apply before_image
          write CLR(undo_next_lsn = record.prev_lsn)
      write ABORT record
}
```

Determinism: re-entering recovery N times produces the same final state (idempotent via CLRs).

### Page LSN

Each page (DBF, ADT, CDX, NTX, ADI, memo) carries an 8-byte LSN at the end of its page footer. Cost: 8 bytes per page. Required to skip already-applied redo work.

**Compatible mode exception.** DBF / CDX / NTX pages in Compatible mode do not carry an inline LSN footer (it would break byte compatibility with Clipper / FoxPro applications). Workaround: in Compatible mode `TxLog` keeps a separate `lsn_table` (overlay file `.lsnmap`) instead of inlining the LSN. The cost is an extra page of I/O per commit, which is acceptable.

### Checkpointing

A background thread:

- Runs every 30 s or every 1000 transactions (configurable).
- Writes `CHECKPOINT_BEGIN` with a snapshot of `active_tx_table` and `dirty_page_table`.
- Flushes dirty pages with `lsn ≤ checkpoint_lsn`.
- Writes `CHECKPOINT_END`.
- Truncates `openads.txlog.<n>` archives older than the checkpoint.

### Recoverable vs unrecoverable errors

| Situation | Action |
|-----------|--------|
| Crash with pending tx | Recovery rollback |
| CRC failure on a log record | Stop recovery at the last valid record, log a warning |
| Missing log file at startup | Assume clean shutdown (legacy ACE behaviour) |
| Page LSN > log tail LSN | Log corruption → halt, requires manual intervention |
| Out of space during commit | Force partial fsync → mark log full → reject new tx until space is freed |

### Tests

- `tx_log_test.cpp` — unit: write / read / CRC / replay.
- `tx_recover_test.cpp` — inject a crash at every LSN boundary, verify consistency.
- `tools/tx-replay <log>` — human-readable dump and dry-run replay.
- Conformance: simulate `AdsBegin / Update / Crash` with `kill -9` mid-write, verify a consistent restart.

## SQL engine internals

### High-level pipeline

```
SQL string
  ▼  Lexer        (DFA, ~150 keywords, xBase + ANSI SQL)
tokens
  ▼  Parser       (recursive descent, Pratt for expressions)
AST
  ▼  Resolver     (name binding, type check, * expansion, qualification)
Bound AST
  ▼  Planner      (logical plan: relational algebra tree)
LogicalPlan
  ▼  Optimizer    (rules + cost model)
PhysicalPlan
  ▼  Executor     (Volcano: open / next / close iterators)
Result rows / row count
```

### Lexer

Hand-written DFA. Recognises:

- Case-insensitive keywords (~150).
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*`, plus delimited `[name]` and `"name"`.
- Literals: int, float, string (ANSI plus `''` escape), date `{^YYYY-MM-DD}`, boolean `.T.` / `.F.` (xBase).
- Operators: ANSI plus xBase `=`, `==`, `!=`, `#`, `$` (substring contains), `||`.
- Parameters: `?` positional, `:name` named.

Output: stream of `Token { kind, lexeme, line, col }`. Errors carry source position.

### Parser

Recursive descent plus a Pratt parser for expressions (ANSI precedence plus xBase `$` and `||`).

EBNF grammar in `docs/sql-grammar.md`. Key productions:

```ebnf
SelectStmt   := WithClause? "SELECT" SelectList FromClause? WhereClause?
                GroupByClause? HavingClause? OrderByClause?
                LimitClause? (CompoundOp SelectStmt)?
FromClause   := "FROM" TableRef ("," TableRef)*
TableRef     := QualifiedName ("AS"? Alias)?
              | "(" SelectStmt ")" "AS"? Alias                  -- derived
              | TableRef JoinType TableRef "ON" Expr            -- join
JoinType     := "INNER" | "LEFT" "OUTER"? | "RIGHT" "OUTER"? |
                "FULL" "OUTER"? | "CROSS"
CompoundOp   := "UNION" "ALL"? | "INTERSECT" | "EXCEPT"
Expr         := PrimaryExpr (InfixOp Expr)*                     -- Pratt
PrimaryExpr  := Literal | ColumnRef | FuncCall | CaseExpr |
                "(" Expr ")" | SubQuery | Parameter |
                "CAST" "(" Expr "AS" TypeName ")"
CaseExpr     := "CASE" Expr? ("WHEN" Expr "THEN" Expr)+
                ("ELSE" Expr)? "END"
```

The AST is built from sum types (`std::variant`) per category. Visitor pattern for passes.

### Resolver

- **Name binding.** Tables are looked up via `Catalog` (DD-aware when the connection has a DD). Columns are searched in the current scope, including CTEs, derived tables, and `JOIN USING`.
- **`*` expansion.** `SELECT *` becomes a list of `ColumnRef`; `t.*` expands only columns of `t`.
- **Type check.** Arithmetic on numeric, concatenation on character, comparison on compatible types. xBase coercions are permissive (`numeric` ↔ `string` implicit conversion is configurable).
- **Aggregate detection.** Marks expressions as aggregate or scalar; validates `HAVING` vs `WHERE`.
- **Subquery scope.** Correlated references are annotated with `outer_scope_depth`.
- **Errors.** `AE_PARSE_ERROR` (7200), `AE_COLUMN_NOT_FOUND` (5063), and so on.

### Planner (logical)

Generates a relational algebra tree of Volcano nodes:

```
LogicalNode = Scan(table)
            | Filter(child, pred)
            | Project(child, exprs)
            | Sort(child, keys)
            | TopN(child, k)
            | Aggregate(child, group_keys, aggs)
            | Distinct(child)
            | Join(left, right, kind, pred)
            | Union(left, right, all?)
            | CTE(name, child)
            | Insert / Update / Delete (table, source, set, pred)
```

Canonical construction first, no optimisation.

### Optimizer

Rule and cost passes:

| Pass | Type | Description |
|------|------|-------------|
| `predicate_pushdown` | rule | Pushes Filter below Join / Project when column refs allow. |
| `column_pruning` | rule | Project drops columns not used upstream. |
| `constant_folding` | rule | Evaluates constant expressions. |
| `predicate_simplify` | rule | `x AND TRUE → x`, `NOT NOT x → x`, etc. |
| `index_selection` | cost | Matches Filter predicates against available indexes → IndexScan. |
| `join_order` | cost | Selinger-style DP up to 8 tables, greedy beyond. |
| `join_method` | cost | NLJ vs HashJoin vs MergeJoin based on cardinality and ordering. |
| `sort_avoidance` | rule | If the Sort key is a prefix of the IndexScan order, drop the Sort. |
| `aggregate_pushdown` | rule | Pre-aggregates locally when group keys are a subset of the partition. |
| `topn_pushdown` | rule | `LIMIT k` paired with index order becomes early-stop IndexScan. |

Cost model:

- Row-count estimation uses per-table statistics in `Catalog` (recno plus simple equi-width index histograms).
- I/O cost is page reads (CdxIndex 8 KB, ADT 4 KB typical).
- CPU cost is `row_count × per-op constant`.

### PhysicalPlan / Executor

Volcano iterators. Each `next()` returns `Optional<Row>`.

Physical operators:

| Operator | Description |
|----------|-------------|
| `SeqScanOp` | Iterates a `Cursor` over a `Table`, reading via L4 `Table::scan()`. |
| `IndexScanOp` | `Cursor` follows `Index::seek_range()`. |
| `IndexLookupOp` | Nested-loop join inner side via index seek. |
| `FilterOp` | Predicate evaluation, drops false rows. |
| `ProjectOp` | Per-row expression evaluation. |
| `SortOp` | External merge sort, runs in `<data-dir>/openads.tmp.<N>`. |
| `TopNOp` | Min-heap with k slots. |
| `HashAggregateOp` | Hash-table aggregation. |
| `StreamAggregateOp` | Input already ordered by group keys. |
| `DistinctOp` | Hash set. |
| `NestedLoopJoinOp` | Outer × inner (with index seek when available). |
| `HashJoinOp` | Builds a hash on the smaller side, probes. |
| `MergeJoinOp` | Both sides ordered (after `sort_avoidance`). |
| `UnionOp` / `UnionAllOp` | Concat plus optional dedupe. |
| `CTEScanOp` | Reuses a materialised CTE result. |
| `InsertOp` / `UpdateOp` / `DeleteOp` | DML, writes via `Table` API and `TxLog`. |

External sort: runs up to `mem_budget` (default 64 MB), spills to tempfiles, K-way merge.

Hash join build: if the hash table exceeds the budget, falls back to Grace Hash (partition plus spill).

### xBase scalar UDFs

Registered in `src/sql/func/scalar.cpp`. Subset (~80 functions):

```
String : LEFT, RIGHT, SUBSTR, AT, RAT, LTRIM, RTRIM, ALLTRIM, PADL, PADR,
         PADC, REPL, SPACE, UPPER, LOWER, STUFF, STRTRAN, LIKE, MATCH
Date   : CTOD, DTOS, DTOC, STOD, DAY, MONTH, YEAR, DOW, CMONTH, CDOW,
         DATE, TIME, NOW, DATEADD, DATEDIFF
Numeric: STR, VAL, INT, ROUND, MOD, ABS, MAX, MIN, EXP, LOG, SQRT,
         SIGN, FLOOR, CEILING
Logic  : IIF, IsNULL, COALESCE, NULLIF, CASE
Type   : CAST, CONVERT, EMPTY, TYPE
Misc   : RECNO, RECCOUNT, DELETED, FOUND, EOF, BOF, LASTREC,
         FIELDNAME, FIELDPOS, FCOUNT
```

Aggregates: `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `STDDEV`, `VARIANCE`, plus xBase `TOTAL`.

The UDF registry allows AEP plugins to add custom functions.

### AEP host (stored procedures)

Advantage Extended Procedures are `.dll` / `.so` plugins exposing a C ABI. Loading and invocation:

```cpp
// AEP plugin entry (mirrored from the ADS spec):
extern "C" UNSIGNED32 GetProcInfo(...);
extern "C" UNSIGNED32 InvokeProc(IInvokeContext*);

// IInvokeContext = ABI exposed by OpenADS to the plugin:
struct IInvokeContext {
    UNSIGNED32 (*GetInputRowFieldCount)(...);
    UNSIGNED32 (*GetInputRowField)(...);
    UNSIGNED32 (*WriteOutputRow)(...);
    UNSIGNED32 (*OpenTable)(...);
    // ... ~30 functions
};
```

`AepHost` (`src/sql/aep/host.cpp`):

- Resolves `dll_name + entry` from DD properties `ADS_DD_PROC_DLL_*`.
- Lazy `dlopen` / `LoadLibrary` on first call.
- Marshals input / output arguments through a stable ABI.
- Sandboxing is optional; the plugin runs in-process (matching original ACE — no sandbox).

SQL invocation: `EXECUTE PROCEDURE my_proc(:p1)` causes the planner to emit `AepCallOp`.

Triggers (`BEFORE` / `AFTER INSERT / UPDATE / DELETE`) are AEP plugins fired by `Table::write_record` during DML.

### Prepared statements and cursors

```
AdsPrepareSQL(hStmt, sql)
  → parse + resolve + plan + optimize, caches the PhysicalPlan
  → parameter binding deferred
AdsExecuteSQL(hStmt)
  → binds parameters → executes → returns Cursor
```

Cursor types:

- **Forward-only** (default): `next()` only.
- **Scrollable** (`AdsCacheRecords ON`): materialised in a tempfile, supports `AdsGotoRecord(n)` and `AdsSkip(-N)`.

`PlanCache` (LRU, key = `SQL hash + schema_version`) avoids re-planning on repeats.

### Result rows backed by a cursor

`AdsGetField(hCursor, fieldName, ...)` reads the current row of the cursor. The cursor keeps a pointer to the row buffer (zero-copy on scans, copy when computed).

### Errors and codes

Mapped to existing ACE codes:

- `7200` AE_PARSE_ERROR
- `7201` AE_INVALID_SQL_TOKEN
- `5063` AE_COLUMN_NOT_FOUND
- `5066` AE_TABLE_NOT_FOUND
- `7041` AE_TYPE_MISMATCH
- `7042` AE_DIVISION_BY_ZERO
- and so on.

### Tests

- `sql_lexer_test.cpp`, `sql_parser_test.cpp` — grammar coverage.
- `sql_resolver_test.cpp` — name-binding edge cases.
- `sql_planner_test.cpp` — golden-file plans for canonical queries.
- `sql_optimizer_test.cpp` — verifies that rules fired (snapshot of the post-optimizer plan).
- `sql_executor_test.cpp` — end-to-end against fixtures with expected results.
- `aep_host_test.cpp` — a sample `.dll` / `.so` plugin returning fixed rows.
- `sql_conformance/` — Advantage SQL test suite derived from official ADS documentation samples.

## Error handling

### Internal model

Internal C++ code uses `Result<T>` (an `expected`-style type in `src/util/result.h`) so error paths are explicit and exceptions are reserved for true programming bugs (`assert`-equivalent contract violations).

```cpp
template<class T> using Result = std::expected<T, Error>;

struct Error {
    int32_t  code;        // ACE error code (e.g. 5012)
    int32_t  sub_code;    // OS errno / GetLastError when applicable
    std::string message;  // formatted, localised
    std::string context;  // file, table, recno, sql snippet
};
```

Errors propagate via early-return; no exception unwinding through L4.

### ACE error code surface

The L1 ABI returns the canonical `UNSIGNED32` ACE error code on every call. OpenADS reproduces the documented ranges so existing apps reading `AdsGetLastError` see identical numbers:

| Range | Family | Examples |
|-------|--------|----------|
| 4000–4999 | Transport / connection | `4001 AE_NETWORK_ERROR`, `4097 AE_INVALID_CONNECTION_HANDLE` |
| 5000–5999 | Engine / locking / records | `5012 AE_LOCKED`, `5036 AE_NO_CONNECTION`, `5063 AE_COLUMN_NOT_FOUND`, `5066 AE_TABLE_NOT_FOUND` |
| 6000–6999 | Index / order | `6105 AE_INDEX_NOT_FOUND`, `6106 AE_INDEX_CORRUPT` |
| 7000–7999 | SQL / parser / type | `7041 AE_TYPE_MISMATCH`, `7200 AE_PARSE_ERROR`, `7201 AE_INVALID_SQL_TOKEN` |

A canonical table lives in `src/abi/error_codes.h`, generated from the documented ACE constants. Anything not yet implemented returns `5004 AE_FUNCTION_NOT_AVAILABLE` rather than a fabricated code, so apps and `rddads` can degrade gracefully.

### `AdsGetLastError` semantics

ACE keeps a per-thread last-error slot. OpenADS replicates this:

```cpp
thread_local Error g_last_error;

extern "C" UNSIGNED32 AdsGetLastError(UNSIGNED32* pulErr,
                                      UNSIGNED8* pucBuf,
                                      UNSIGNED16* pusBufLen) {
    *pulErr = g_last_error.code;
    copy_text(pucBuf, pusBufLen, g_last_error.message);
    return AE_SUCCESS;
}
```

Every L1 thunk updates the slot before returning. Successful calls clear it.

### Localised messages

Default English. Optional catalogues for Spanish and Portuguese (large legacy ADS user bases). Selected via `AdsSetLocalizedMessages` or env `OPENADS_LOCALE=es`. Catalogues live in `src/abi/messages_<locale>.cpp`, lookup by `code`.

### Logging / tracing

Three levels controlled by env:

| Var | Effect |
|-----|--------|
| `OPENADS_LOG=info` | Connection open / close, table open, SQL executed (truncated) |
| `OPENADS_LOG=debug` | Plus index seeks, locks acquired, tx boundaries |
| `OPENADS_LOG=trace` | Every L1 entry / exit with arguments and return code |
| `OPENADS_LOG_FILE=<path>` | Redirect log; default `stderr` |

`tools/ace-trace` is a separate shim that intercepts every `Ads*` call and writes a structured trace; useful for diffing against original ACE behaviour.

### Failure boundaries

| Boundary | Strategy |
|----------|----------|
| L5 (OS) errors | Map errno / `GetLastError` into `Error.sub_code`, surface a 4xxx or 5xxx ACE code |
| L4 corruption (CRC / LSN mismatch) | Halt access to the affected file, return `5103 AE_TABLE_CORRUPTED`, log details |
| L3 SQL errors | Return 7xxx range, no abort of the connection |
| L2 invalid handle | `4097 AE_INVALID_CONNECTION_HANDLE`, no crash |
| L1 panic (assert) | Last-resort: log and `abort()` only on contract violations during debug builds; release builds convert to `5000 AE_INTERNAL_ERROR` |

## Testing strategy and roadmap

### Test pyramid

| Layer | Tools | Coverage target |
|-------|-------|-----------------|
| Unit | doctest, run on every commit | ≥ 85 % per module (engine, drivers, sql, lock, tx) |
| Integration (in-process) | doctest, real files | Full driver matrix (ADT / CDX / NTX / VFP) × open / write / index / memo / tx |
| ABI conformance | C harness invoking L1 entry points | All ~230 ACE entry points exercised at least once |
| Harbour smoke | `harbour_smoke.prg` linked against `rddads` and OpenADS DLL | `tests/datad.prg`, `tests/manage.prg` from `c:\harbour\contrib\rddads\tests\` plus custom xBase scenarios |
| Byte compatibility | `tools/adt-dump`, `tools/cdx-dump` diff vs ACE-produced fixtures | All write paths produce byte-identical output |
| Multi-process | Two OpenADS plus optional original ACE on shared files | No corruption, lock semantics match |
| Fuzzing | libFuzzer over lexer / parser / log replay / driver readers | Zero crashes / UB after N hours |
| Benchmarks | google-benchmark micro-suite | No regression vs previous tag |
| Recovery | Crash-injection harness (`kill -9` between LSN boundaries) | Recovery converges deterministically |

### CI matrix

GitHub Actions:

- Compilers: MSVC 2022 (x86 / x64), GCC 11+, Clang 14+, MinGW-w64.
- OS: Windows 2022, Ubuntu 22.04, macOS 13, FreeBSD 14 (via cross / VM).
- Sanitisers: ASan, UBSan, TSan on Linux Clang job.
- Nightly: full Harbour rddads test suite plus byte-compat job against pinned ACE-produced fixtures.

### Phase 1 milestones

| Milestone | Deliverable |
|-----------|-------------|
| **M0 — skeleton** | CMake + L5 platform + util + doctest harness. Builds on Win / Linux / macOS. |
| **M1 — DBF read** | `dbf_common` + CDX driver read-only, no index. `AdsConnect60` / `AdsOpenTable` / `AdsGotoTop` / `AdsSkip` / `AdsGetField` work over a CDX-typed DBF. |
| **M2 — DBF write + lock** | Append / update / delete + `LockMgr` Compatible mode. NTX driver. Single-process integrity tests. |
| **M3 — indexes** | CDX read / write, NTX read / write, ADI scaffolding. Seek, scope, AOF basics. |
| **M4 — ADT + memo** | ADT driver full, `.adm` / `.fpt` / `.dbt` memo stores. VFP driver. Encryption AES-128 / 256. |
| **M5 — TPS** | TxLog WAL + recovery, savepoints, multi-table atomicity, group commit. Compatible-mode `.lsnmap` overlay. |
| **M6 — DD** | `.add` reader / writer, users / groups / RI / views / procs metadata, `AdsConnect60` to a DD. |
| **M7 — SQL** | Lexer / parser / resolver / planner / optimizer / executor. xBase UDFs. AEP host. Triggers. |
| **M8 — Conformance** | Full Harbour `tests/datad.prg` and `tests/manage.prg` green. Byte-compat job green. Multi-process green. First tagged release `0.1.0`. |

Phase 2 (post-1.0): TCP server reusing L2-L5, wire-protocol design, replication, AIS / HTTP gateways. Out of scope for this document.

## Next steps

Phase 1 is broken into nine independently shippable milestones (`M0`–`M8`). Each milestone gets its own implementation plan under `docs/superpowers/plans/`, written in TDD bite-sized form so any contributor can pick it up.

| Milestone | Plan | Status |
|-----------|------|--------|
| **M0 — Skeleton** | [`2026-05-03-openads-m0-skeleton.md`](docs/superpowers/plans/2026-05-03-openads-m0-skeleton.md) | **Done.** CMake project, L5 platform layer (file / lock / mmap / path / time / thread), `util/Result<T>` / `Span<T>` / `Log`, doctest harness (27 cases / 77 assertions), GitHub Actions matrix (Windows / Linux / macOS). |
| **M1 — DBF read (CDX)** | [`2026-05-03-openads-m1-dbf-read.md`](docs/superpowers/plans/2026-05-03-openads-m1-dbf-read.md) | **Done.** Read-only DBF (`ADS_CDX` typed) via `AdsConnect60` / `AdsOpenTable` / `AdsGotoTop` / `AdsSkip` / `AdsGetField` and friends. No memo (M4), no index (M3), no write (M2). |
| **M2 — DBF write + LockMgr** | [`2026-05-03-openads-m2-dbf-write-lock.md`](docs/superpowers/plans/2026-05-03-openads-m2-dbf-write-lock.md) | **Done.** Append / update / delete on CDX- and NTX-typed DBFs, `LockMgr` Compatible-mode byte ranges (NTX `1_000_000_000`, CDX `0x7FFFFFFE - recno`), single-process integrity tests. No pack / zap (M3), no memo (M4), no TPS (M5). |
| **M3 — Indexes** | [`2026-05-03-openads-m3-indexes.md`](docs/superpowers/plans/2026-05-03-openads-m3-indexes.md) | **Partial — round-trips OpenADS-produced files only.** NTX header + leaf read+write+create works against indexes that OpenADS itself wrote. Multi-leaf NTX split, branch descent, and FoxPro CDX byte-compat are blocked by issues tracked in [`docs/known-issues.md`](docs/known-issues.md). Fixes land in **M3.6**. `Order` + `Scope` on `Table`, 15 ACE entry points, AOF/Pack/Zap stubs are all in place. |
| **M3.5 — CDX index** | (extends M3) | **Partial — non-standard byte layout.** A working compact-leaf encoder/decoder using a hardcoded 24/8/8-bit split. Round-trips OpenADS-produced `.cdx` files; **does NOT match FoxPro byte layout** (bit widths must derive from `keylen`, tag directory must use the compound structure tag). M3.6 replaces this with a real FoxPro-equivalent encoder driven by Harbour `hb_cdxPageLeafInitSpace`. See `docs/known-issues.md` items 1-3. |
| **M3.6 — Real index byte-compat** | (in flight) | **Partial.** **Done:** CDX leaf encoder now uses Harbour-equivalent `compute_layout()` modelled on `hb_cdxPageLeafInitSpace` (bBits derived from key length; for `keylen=4` the result is 18/3/3 bits packed in 3 bytes). Tightened `AdsOpenIndex` / `AdsCreateIndex` lifecycle (prior bindings cleared before `set_order`). `AdsCreateIndex` now skips deleted records. NTX `unique` and `descending` flags round-trip through create/reopen. **Pending:** CDX compound structure-tag directory, CDX big-endian branch descent at the right offset, NTX multi-level split, soft-seek past-end fix. See `docs/known-issues.md`. |
| **M4 — ADT + memo + VFP + AES** | [`2026-05-03-openads-m4-adt-memo-vfp-aes.md`](docs/superpowers/plans/2026-05-03-openads-m4-adt-memo-vfp-aes.md) | **Partial.** **Done:** AES-128 / AES-256 ECB via vendored tiny-AES-c, validated against FIPS-197 Appendix B (AES-128) and NIST SP 800-38A F.1.5/F.1.6 (AES-256) test vectors. DBT memo real (dBase III/Clipper, 512-byte blocks, `0x1A 0x1A` terminator, multi-block walk). FPT memo real (FoxPro/VFP, big-endian header, 8-byte block headers, configurable block size 64/512). `Table::attach_memo` routes M-type field reads/writes through the memo store; `Connection::open_table` auto-attaches `.dbt` / `.fpt` siblings when M-fields are present. ABI thunks: `AdsGetMemoLength`, `AdsGetMemoDataType`, `AdsBinaryToFile`, `AdsFileToBinary` are live. `AdsGetLastAutoinc` returns 0 stub. Encryption ABI (`AdsEnableEncryption` / `AdsEncryptTable` / `AdsEncryptRecord` / etc.) returns `AE_FUNCTION_NOT_AVAILABLE` until the ADS encryption-mode is reverse-engineered (the AES primitive itself is ready). **Pending:** ADT format (ADS proprietary, requires research), VFP driver autoinc/NULL bitmap extensions, ADM memo (ADS proprietary), AES record-encryption boundary on `Table`. |
| **M5 — TPS / WAL** | TBD | **Tx + WAL + crash recovery + savepoints landed.** ABI: `AdsBeginTransaction`, `AdsCommitTransaction`, `AdsRollbackTransaction`, `AdsInTransaction`, `AdsCreateSavepoint`, `AdsRollbackTransaction80`. Each tx event writes a record to `openads.txlog` in the data dir (`BEGIN` / `UPDATE` / `COMMIT` / `ABORT`, CRC-32C protected). UPDATE records carry the table relative path + before/after images. `Connection::open` runs recovery: any tx without `COMMIT` or `ABORT` is replayed by writing back before-images and appending `ABORT`, then the log is truncated. Savepoints are an in-memory ordered-op log layered on top of the before-image map; `AdsRollbackTransaction80` with a savepoint name does a partial rollback, with `nullptr` it falls back to a full rollback. Smoke tests cover crash mid-tx + recovery and partial rollback through a savepoint. **Pending:** group commit (batched fsync), page-LSN tracking with `.lsnmap` overlay for Compatible mode, savepoint persistence in WAL. |
| **M6 — Data Dictionary** | TBD | **Alias resolution landed (OpenADS-native DD format).** `engine::DataDict` is a UTF-8 text file (`# OpenADS Data Dictionary v0` + `TABLE alias=path` lines) created in the data dir. `Connection::open` accepts either a directory path or a `.add` path; in the latter case it loads the DD and auto-resolves aliases passed to `AdsOpenTable`. ABI: `AdsDDCreate`, `AdsDDAddTable`, `AdsDDRemoveTable`. Smoke covers create -> add -> open-by-alias -> reopen. **Pending:** ADS proprietary `.add` binary format (research-deferred), users / groups / permissions, RI rules, views, stored procedures, validation expressions, default values. |
| **M7 — SQL engine** | TBD | **M7.5 landed (`SELECT *` + AND-joined `WHERE` with all six operators).** `engine::sql::parse_select` parses `SELECT * FROM <table> [WHERE <cmp> [AND <cmp>]*]` where each `<cmp>` is `<col> <op> '<literal>'` and `<op>` is one of `=`, `!=`, `<>`, `<`, `>`, `<=`, `>=`. `Table` gained a `RowPredicate` slot; `goto_top` / `skip` automatically advance past non-matching records when a filter is attached. ABI: `AdsExecuteSQLDirect` lowers each WHERE term into `(field_index, op, literal)` and the closure short-circuits the AND. Projection lists, OR / NOT / parens, numeric literals, ORDER BY, joins, aggregates, subqueries, and UDFs return `AE_PARSE_ERROR`. **Pending:** full Advantage SQL grammar (lexer + AST + planner + executor), xBase UDFs (LEFT, SUBSTR, CTOD, ...), AEP host for stored procedures, triggers, INSERT / UPDATE / DELETE / CREATE TABLE. |
| **M8 — Conformance + 0.1.0** | TBD | Full Harbour `tests/datad.prg` and `tests/manage.prg` green, byte-compat job green, multi-process scenario green, first tagged release. |

### Snapshot

- **135 doctest cases / 1820 assertions passing** on Windows / MSVC 2022 Release.
- **~80 ACE entry points wired** (read / write / lock / index / scope / memo / encryption / autoinc / transaction / savepoint / data dictionary / SQL).
- **Persistent WAL with crash recovery** is byte-identical for OpenADS-produced files.
- **Live tags:** `m0-done`, `m1-done`, `m2-done`, `m3-done`, `m3.5-done`, `m3.6-partial`, `m3.7-partial`, `m3.7-closed`, `m3.8-partial`, `m3.9-partial`, `m3.10-partial`, `m4-partial`, `m5-partial`, `m5.1-partial`, `m5.2-partial`, `m5.3-partial`, `m5.4-partial`, `m5.5-partial`, `m6-partial`, `m7.1-partial`, `m7.2-partial`, `m7.3-partial`, `m7.4-partial`, `m7.5-partial`, `m8.0-partial`.
- **Drop-in DLL:** `ace64.dll` (Win x64) and `ace32.dll` (Win x86) build from the `openads_ace` SHARED target, exporting the 80 `Ads*` entry points required by Harbour `contrib/rddads`.

### Working on a milestone

1. Brainstorm the milestone briefly against the spec above to surface anything that changed since the original design was written.
2. Write its implementation plan into `docs/superpowers/plans/YYYY-MM-DD-openads-mN-<topic>.md` using the same TDD bite-sized template as M0.
3. Execute the plan task by task. Each task is `red → green → commit` and lands one focused change.
4. When the milestone is done, mark it green in the table above, push, and tag the head commit `mN-done` for traceability.

### Immediate next action

Execute `M0` using the saved plan. Two execution paths:

- **Subagent-driven (recommended).** Dispatch a fresh subagent per task with two-stage review between tasks. See `superpowers:subagent-driven-development`.
- **Inline.** Walk the plan in the current session with checkpoints. See `superpowers:executing-plans`.

## Build (M0 skeleton)

```
git clone https://github.com/FiveTechSoft/OpenADS.git
cd OpenADS
cmake --preset default
cmake --build build/default
ctest --preset default --output-on-failure
```

Other presets: `debug`, `msvc-x64`, `ninja-clang` — see `CMakePresets.json`.

## License

MIT.
