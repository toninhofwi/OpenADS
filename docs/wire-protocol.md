---
title: Wire protocol
layout: default
parent: Home (EN)
nav_order: 4
permalink: /en/wire-protocol/
---

# OpenADS Wire Protocol — v1.0.0-rc25

This document specifies the OpenADS-native wire protocol spoken
between an OpenADS client (`ace64.dll` opened with a
`tcp://host:port/<dir>` URI) and an OpenADS server
(`tools/serverd/openads_serverd` or the `network::Server` library
embedded in another process).

The protocol is **not byte-compatible** with the proprietary
Advantage Database Server remote protocol. OpenADS implements its
own clean-room wire format because publishing or implementing the
SAP-owned protocol would require disassembly or other material
covered by the Advantage SDK / ACE EULA.

This spec is the canonical reference for downstream consumers
that want to write a non-C++ client (Python, Go, Rust, Harbour
extension hosts) without reading the C++ source. The on-the-wire
byte layout has only grown — opcode bytes are stable; new opcodes
get appended.

---

## 1. Transport

- **TCP/IP** over an arbitrary port (no IANA allocation; the
  server binds to whatever its CLI / API caller picks). The
  reference daemon (`openads_serverd`) defaults to
  `127.0.0.1:6262`.
- **Plaintext** (`tcp://...`) or **TLS** (`tls://...`). The TLS
  transport landed in v0.4.0 (M12.12 / M12.13) via vendored
  `mbedtls 3.6 LTS` (Apache-2.0, statically linked since v1.0.0-rc8 —
  no runtime `libssl` / `libcrypto` / `mbedtls` DLL dependency).
- **No multiplexing.** One connection = one session = one logical
  database connection. Statements + cursors are scoped to the
  session; multiple parallel SQL queries on the same TCP
  connection are serialised by the client mutex.
- **Nagle disabled** (`TCP_NODELAY`, M12.20 / v1.0.0-rc18) — the
  wire is strict ping-pong, so Nagle's accumulation delay was pure
  latency tax.
- **No multiplexing.** One connection = one session = one logical
  database connection. Statements + cursors are scoped to the
  session; multiple parallel SQL queries on the same TCP
  connection are serialised by the client mutex.

## 2. Frame layout

Every message is a single frame:

```
+--------+--------+--------+--------+--------+--------+ ... +--------+
|     payload length (BE u32)        | opcode |    payload bytes      |
+--------+--------+--------+--------+--------+--------+ ... +--------+
   bytes 0..3 (length)                  byte 4    bytes 5..(4+len)
```

- **`payload length`** — 32-bit unsigned, **big-endian**, counts only
  the payload bytes (excludes the 5-byte header). 0 ⇒ no payload.
- **`opcode`** — 8-bit unsigned. See §4 for the full list.
- **`payload`** — opcode-specific. Numeric integers inside the
  payload are **little-endian** unless explicitly noted (e.g. the
  4-byte BE length in the header). Strings are raw UTF-8 / OEM
  bytes with no NUL terminator unless an explicit length prefix
  precedes them.

## 3. Session lifecycle

```
client                                 server
  |                                       |
  |--Hello---------------------- --------->|
  |<------------------ ---------HelloAck---|   (banner = "openads/<ver>")
  |                                       |
  |--Connect(dir,user,pw)----------------->|
  |<-------------------- -----ConnectAck---|   ("connected:<dir>")
  |                                       |
  |  ... opcode pairs (OpenTable / SQL /  |
  |      Fetch / Skip / GetField / ...)   |
  |                                       |
  |--Disconnect--------------------------->|
  |   (server closes socket)              |
```

**Hello** is optional from a strict-protocol point of view (the
reference client skips it and goes straight to Connect), but the
server always answers with the banner if asked.

**Connect** is mandatory before any table / SQL op. After
ConnectAck the session has an `engine::Connection` open against
the requested data dir.

**Disconnect** triggers an immediate server-side close with full
cleanup (cursors, ABI statement, ABI connection). A peer-close
without Disconnect also runs cleanup.

## 4. Opcodes

The byte values are stable; new opcodes only get appended.

The table below is generated from the canonical `Opcode` enum in
`src/network/wire.h`. Opcodes are listed in hex order; note the
byte values are **not** contiguous with milestone order — later
milestones reused gaps left by earlier ones.

| Op | Hex | Direction | Meaning | Milestone |
|----|-----|-----------|---------|-----------|
| `Hello`               | `0x01` | C→S | Banner request                  | M12.3 |
| `HelloAck`            | `0x02` | S→C | Banner reply                    | M12.3 |
| `Connect`             | `0x10` | C→S | Open session                    | M12.3 |
| `ConnectAck`          | `0x11` | S→C | Session opened                  | M12.3 |
| `Disconnect`          | `0x12` | C→S | Close session                   | M12.3 |
| `OpenTable`           | `0x20` | C→S | Open a DBF/CDX/NTX              | M12.4 |
| `OpenTableAck`        | `0x21` | S→C | Returns wire table-id           | M12.4 |
| `CloseTable`          | `0x22` | C→S | Close table                     | M12.4 |
| `CloseTableAck`       | `0x23` | S→C |                                 | M12.4 |
| `ExecuteSQL`          | `0x30` | C→S | Run SQL statement               | M12.7 |
| `ExecuteSQLAck`       | `0x31` | S→C | Returns cursor table-id (or 0)  | M12.7 |
| `Fetch`               | `0x32` | C→S | Batch row read                  | M12.11 |
| `FetchAck`            | `0x33` | S→C | Row matrix                      | M12.11 |
| `GotoTop`             | `0x40` | C→S |                                 | M12.4 |
| `GotoTopAck`          | `0x41` | S→C |                                 | M12.4 |
| `Skip`                | `0x42` | C→S | Skip ±N rows                    | M12.4 |
| `SkipAck`             | `0x43` | S→C |                                 | M12.4 |
| `GetField`            | `0x44` | C→S | Read one column at cursor       | M12.4 |
| `GetFieldAck`         | `0x45` | S→C | Column bytes                    | M12.4 |
| `GetRecordCount`      | `0x46` | C→S |                                 | M12.4 |
| `GetRecordCountAck`   | `0x47` | S→C |                                 | M12.4 |
| `AtEOF`               | `0x48` | C→S |                                 | M12.4 |
| `AtEOFAck`            | `0x49` | S→C | 0 / 1 byte                      | M12.4 |
| `DescribeTable`       | `0x4A` | C→S | Schema in one round-trip        | M12.14 |
| `DescribeTableAck`    | `0x4B` | S→C | Column list + types             | M12.14 |
| `AtBOF`               | `0x4C` | C→S |                                 | M12.14 |
| `AtBOFAck`            | `0x4D` | S→C | 0 / 1 byte                      | M12.14 |
| `GetRecordNum`        | `0x4E` | C→S | Current recno                   | M12.14 |
| `GetRecordNumAck`     | `0x4F` | S→C |                                 | M12.14 |
| `AppendBlank`         | `0x50` | C→S |                                 | M12.6 |
| `AppendBlankAck`      | `0x51` | S→C |                                 | M12.6 |
| `SetField`            | `0x52` | C→S | Write one column at cursor      | M12.6 |
| `SetFieldAck`         | `0x53` | S→C |                                 | M12.6 |
| `DeleteRecord`        | `0x54` | C→S | Mark deleted                    | M12.6 |
| `DeleteRecordAck`     | `0x55` | S→C |                                 | M12.6 |
| `RecallRecord`        | `0x56` | C→S | Undelete                        | M12.6 |
| `RecallRecordAck`     | `0x57` | S→C |                                 | M12.6 |
| `GotoRecord`          | `0x58` | C→S | Jump to recno                   | M12.6 |
| `GotoRecordAck`       | `0x59` | S→C |                                 | M12.6 |
| `FlushTable`          | `0x5A` | C→S | Force write-through             | M12.6 |
| `FlushTableAck`       | `0x5B` | S→C |                                 | M12.6 |
| `Reindex`             | `0x60` | C→S | Rebuild bound indexes           | M12.8 |
| `ReindexAck`          | `0x61` | S→C |                                 | M12.8 |
| `IsRecordDeleted`     | `0x62` | C→S |                                 | M12.14 |
| `IsRecordDeletedAck`  | `0x63` | S→C | 0 / 1 byte                      | M12.14 |
| `GotoBottom`          | `0x64` | C→S |                                 | M12.14 |
| `GotoBottomAck`       | `0x65` | S→C |                                 | M12.14 |
| `IsFound`             | `0x66` | C→S | Seek-hit flag                   | M12.15 |
| `IsFoundAck`          | `0x67` | S→C |                                 | M12.15 |
| `RefreshRecord`       | `0x68` | C→S | Re-read current record          | M12.15 |
| `RefreshRecordAck`    | `0x69` | S→C |                                 | M12.15 |
| `GetTableType`        | `0x6A` | C→S | DBF / CDX / NTX kind            | M12.15 |
| `GetTableTypeAck`     | `0x6B` | S→C |                                 | M12.15 |
| `GetRecordLength`     | `0x6C` | C→S |                                 | M12.15 |
| `GetRecordLengthAck`  | `0x6D` | S→C |                                 | M12.15 |
| `GetNumIndexes`       | `0x6E` | C→S |                                 | M12.15 |
| `GetNumIndexesAck`    | `0x6F` | S→C |                                 | M12.15 |
| `GetLastAutoinc`      | `0x70` | C→S | Last autoinc value              | M12.15 |
| `GetLastAutoincAck`   | `0x71` | S→C |                                 | M12.15 |
| `LockRecord`          | `0x72` | C→S | Single-record byte-range lock   | M12.15 |
| `LockRecordAck`       | `0x73` | S→C |                                 | M12.15 |
| `UnlockRecord`        | `0x74` | C→S |                                 | M12.15 |
| `UnlockRecordAck`     | `0x75` | S→C |                                 | M12.15 |
| `LockTable`           | `0x76` | C→S | Whole-table lock                | M12.15 |
| `LockTableAck`        | `0x77` | S→C |                                 | M12.15 |
| `UnlockTable`         | `0x78` | C→S |                                 | M12.15 |
| `UnlockTableAck`      | `0x79` | S→C |                                 | M12.15 |
| `PackTable`           | `0x7A` | C→S | Compact deleted rows            | M12.15 |
| `PackTableAck`        | `0x7B` | S→C |                                 | M12.15 |
| `ZapTable`            | `0x7C` | C→S | Empty table                     | M12.15 |
| `ZapTableAck`         | `0x7D` | S→C |                                 | M12.15 |
| `FlushFileBuffers`    | `0x7E` | C→S | fsync table + index files       | M12.15 |
| `FlushFileBuffersAck` | `0x7F` | S→C |                                 | M12.15 |
| `CloseAllIndexes`     | `0x80` | C→S |                                 | M12.15 |
| `CloseAllIndexesAck`  | `0x81` | S→C |                                 | M12.15 |
| `SetAOF`              | `0x82` | C→S | Install Rushmore filter         | M12.15 |
| `SetAOFAck`           | `0x83` | S→C | OptLevel + opt-bitmap meta      | M12.15 |
| `ClearAOFRemote`      | `0x84` | C→S | Drop the installed AOF          | M12.15 |
| `ClearAOFRemoteAck`   | `0x85` | S→C |                                 | M12.15 |
| `GetAOFOptLevel`      | `0x86` | C→S |                                 | M12.15 |
| `GetAOFOptLevelAck`   | `0x87` | S→C | FULL / PART / NONE              | M12.15 |
| `OpenIndex`           | `0x88` | C→S | Open `.cdx` / `.ntx` index      | M12.16 |
| `OpenIndexAck`        | `0x89` | S→C | Wire index-id                   | M12.16 |
| `CloseIndex`          | `0x8A` | C→S |                                 | M12.16 |
| `CloseIndexAck`       | `0x8B` | S→C |                                 | M12.16 |
| `SetOrder`            | `0x8C` | C→S | Switch active order by handle   | M12.16 |
| `SetOrderAck`         | `0x8D` | S→C |                                 | M12.16 |
| `SetOrderByName`      | `0x8E` | C→S | Switch active order by tag name | M12.16 |
| `SetOrderByNameAck`   | `0x8F` | S→C |                                 | M12.16 |
| `Seek`                | `0x90` | C→S | Index key seek (hit / miss)     | M12.16 |
| `SeekAck`             | `0x91` | S→C | Found flag + recno              | M12.16 |
| `SeekLast`            | `0x92` | C→S | Seek last matching key          | M12.16 |
| `SeekLastAck`         | `0x93` | S→C |                                 | M12.16 |
| `CreateIndex`         | `0x94` | C→S | CDX-on-the-wire `CREATE INDEX`  | M12.16 |
| `CreateIndexAck`      | `0x95` | S→C |                                 | M12.16 |
| `SkipUnique`          | `0x96` | C→S | Skip to next unique key         | M12.16 |
| `SkipUniqueAck`       | `0x97` | S→C |                                 | M12.16 |
| `SetScope`            | `0x98` | C→S | Set index key-range scope       | M12.16 |
| `SetScopeAck`         | `0x99` | S→C |                                 | M12.16 |
| `ClearScope`          | `0x9A` | C→S |                                 | M12.16 |
| `ClearScopeAck`       | `0x9B` | S→C |                                 | M12.16 |
| `FetchCurrentRow`     | `0x9C` | C→S | Read whole current row          | M12.17 |
| `FetchCurrentRowAck`  | `0x9D` | S→C | Full record buffer              | M12.17 |
| `GetLastTableUpdate`  | `0x9E` | C→S | DBF header last-update stamp    | M12.24 |
| `GetLastTableUpdateAck` | `0x9F` | S→C | Date as `YYYYMMDD` bytes       | M12.24 |
| `MgConnect`           | `0xA0` | C→S | Open management telemetry channel | M9.25 (rc24) |
| `MgConnectAck`        | `0xA1` | S→C | Channel opened / reachability ack | M9.25 (rc24) |
| `MgRequest`           | `0xA2` | C→S | Request a telemetry snapshot    | M9.25 (rc24) |
| `MgReplyAck`          | `0xA3` | S→C | `MgSnapshot` payload            | M9.25 (rc24) |
| `Error`               | `0xFF` | S→C | Any failure (4-byte ACE-code prefix since M12.10) | M12.3 |

## 5. Payload formats

Notation:
- `u8`, `u16`, `u32` — unsigned little-endian unless noted.
- `len-prefixed string` — `[u16 byte_length][bytes...]` (M12.9
  Connect frame uses this form for dir/user/pw).
- `bytes` — raw, length implied by frame length.

### 5.1 Hello / HelloAck
- Hello: empty.
- HelloAck: `bytes` — server banner, e.g. `openads/1.0.0-rc25`.
  Since v1.0.0-rc13 the banner is driven from `git describe`, so
  it always reflects the actual build.

### 5.2 Connect / ConnectAck
- Connect: `[u16 dlen][dir][u16 ulen][user][u16 plen][password]`
  (M12.9 — `user` and `password` may be empty if the server
  doesn't require auth).
- ConnectAck: `bytes` — `connected:<dir>` (informational).

### 5.3 Disconnect
- C→S only. Empty payload. No ack — server closes the socket.

### 5.4 OpenTable / OpenTableAck
- OpenTable: `bytes` — table leaf path (e.g. `data.dbf`),
  resolved against the session's data dir.
- OpenTableAck: `[u32 wire_table_id]` — opaque to the client;
  every subsequent table op echoes this id.

### 5.5 CloseTable / CloseTableAck
- CloseTable: `[u32 wire_table_id]`.
- Ack: empty.

### 5.6 ExecuteSQL / ExecuteSQLAck
- ExecuteSQL: `bytes` — raw SQL text, ASCII / UTF-8.
- ExecuteSQLAck: `[u32 cursor_id]` — `0` for non-SELECT
  (INSERT / UPDATE / DELETE / DDL), otherwise a wire table-id
  the client uses with the read-side ops below.

### 5.7 Fetch / FetchAck
- Fetch: `[u32 tid][u32 max_rows][u8 ncols][per col: u8 nlen, name]`.
  Walks `max_rows` rows from the cursor's current position; works
  for both engine handles (returned by `OpenTable`) and SQL
  cursor handles (returned by `ExecuteSQL`).
- FetchAck: `[u32 nrows][u8 ncols][per row, per col: u16 vlen, val_bytes]`.
  Rows are emitted in cursor order; column order matches the
  request. `nrows` is the number actually returned; may be less
  than `max_rows` (EOF or skip failure stops the walk early).

### 5.8 GotoTop / GotoTopAck, GotoBottom / GotoBottomAck, Skip / SkipAck, GotoRecord / GotoRecordAck
- GotoTop / GotoBottom: `[u32 tid]`.
- Skip: `[u32 tid][u32 step_le]` (`step` is signed; transmit as
  little-endian raw u32 bits).
- GotoRecord: `[u32 tid][u32 recno]`.
- Acks **carry a row trailer** since M12.18 (v1.0.0-rc18):
  `[u32 recno][u8 deleted][u32 row_buf_len][row_buf bytes]`. The
  trailer is empty (length 0) only when the cursor lands at EOF /
  Limbo. Clients that pre-date M12.18 can ignore extra bytes past
  the prior 0-length frame — the wire codec passes the full payload
  through.

### 5.9 GetField / GetFieldAck
- GetField: `[u32 tid][bytes field_name]` (no length prefix —
  field name runs to end of payload).
- Ack: `bytes` — column value as the engine's textual rendering
  (DBF columns are textually formatted on disk; this is the same
  byte stream `AdsGetField` returns locally, including trailing
  blank-padding for fixed-width columns).

### 5.10 GetRecordCount / GetRecordCountAck, AtEOF / AtEOFAck
- GetRecordCount: `[u32 tid]`. Ack: `[u32 record_count]`.
- AtEOF: `[u32 tid]`. Ack: 1 byte (`0` = not EOF, `1` = EOF).

### 5.11 AppendBlank, DeleteRecord, RecallRecord, FlushTable, Reindex, Pack, Zap
- All seven: `[u32 tid]`, ack empty.
- `AppendBlank` since M12.23 / v1.0.0-rc19 auto-acquires a record
  byte-range lock on the new row (ACE semantics for non-exclusive
  tables — X#'s `GoHot` refuses to write a record it sees as
  unlocked).

### 5.12 SetField / SetFieldAck
- SetField: `[u32 tid][u16 namelen][name_bytes][value_bytes]`.
  Value runs from `5 + namelen` to end of payload. The engine
  applies the textual representation through
  `Table::set_field(idx, std::string)`, which handles all
  field types (C / N / D / L / M / V / Q / I / Y / B).
- Ack: empty.

### 5.13 Error
- S→C only. Layout (M12.10 onwards):
  `[u32 ace_code_le][message_bytes]`.
- `ace_code` is one of the constants from `include/openads/error.h`
  (e.g. `5004` AE_FUNCTION_NOT_AVAILABLE, `5018`
  AE_NO_FILE_FOUND, `5066` AE_TABLE_NOT_FOUND, `7077`
  AE_LOGIN_FAILED, `7200` AE_PARSE_ERROR).
- `message` is a human-readable diagnostic; not stable across
  versions, only for debugging / logs.

### 5.14 DescribeTable / DescribeTableAck (M12.14)
- DescribeTable: `[u32 tid]`.
- Ack: `[u8 ncols][per col: u8 nlen, name, u8 type, u16 len, u8 dec]`.
- Client caches the result on `RemoteTable` so the entire field-
  metadata API (`AdsGetNumFields`, `AdsGetFieldName/Type/Length/
  Decimals`) costs one round-trip per opened table.

### 5.15 FetchCurrentRow / FetchCurrentRowAck (M12.17)
- C→S: `[u32 tid]`.
- Ack: same row-trailer layout as the navigation acks (§5.8):
  `[u32 recno][u8 deleted][u32 row_buf_len][row_buf bytes]`.
- The client caches the row buffer and serves every cell read
  (`AdsGetField` / `AdsGetLong` / `AdsGetDouble` / `AdsGetJulian`)
  out of the cache until the next navigation, collapsing W cells
  per row to 1 RTT.

### 5.16 Lock / Unlock (M12.15)
- `LockRecord` / `UnlockRecord`: `[u32 tid][u32 recno]`. `recno == 0`
  means "current record" (M12.23 / rc19).
- `LockTable` / `UnlockTable`: `[u32 tid]`. Ack empty.

### 5.17 SetAOF / ClearAOF / GetAOFOptLevel (M12.15)
- `SetAOF`: `[u32 tid][bytes cond]` — cond is the Clipper-style
  AOF expression text (`TAG = 'AAAA'`, `AGE BETWEEN 25 AND 40`, …).
- `SetAOFAck`: `[u32 opt_level]` (`0` NONE, `1` PART, `2` FULL).
- `ClearAOF`: `[u32 tid]`, ack empty.
- Non-optimisable expressions (since M12.24 / rc21) return
  `opt_level = 0` rather than an Error frame, matching ACE.

### 5.18 OpenIndex / CloseIndex / Seek / CreateIndex (M12.16, M12.16b)
- `OpenIndex`: `[u32 tid][bytes index_path]`.
- `OpenIndexAck`: `[u32 wire_index_id]`. Server promotes an ABI
  index handle in `tbls_h` and syncs the engine cursor.
- `CloseIndex`: `[u32 wire_index_id]`, ack empty.
- `Seek`: `[u32 tid][u32 hindex][u8 soft][u8 last][u16 klen][key]`.
- `SeekAck`: `[u8 found][u32 recno]`.
- `CreateIndex`: `[u32 tid][u16 tlen][tag][u16 elen][expr][u32 flags]`
  (flags = `ADS_DESCENDING` / `ADS_UNIQUE` / `ADS_COMPOUND` /
  `ADS_DOUBLEKEY`).
- Ack: empty.

### 5.19 SetOrder / SetOrderByName (M12.16c)
- `SetOrder`: `[u32 tid][u32 hindex]` — `hindex == 0` clears order.
- `SetOrderByName`: `[u32 tid][bytes tag_name]`.
- Both acks empty.

### 5.20 GetLastTableUpdate / Ack (M12.24)
- C→S: `[u32 tid]`.
- Ack: `[bytes date_str]` — 8 bytes `YYYYMMDD` (raw, no format
  applied; client renders via the process-wide date format set by
  `AdsSetDateFormat`).

### 5.21 MgConnect / MgRequest (M9.25, rc24)

The management telemetry channel — what the `AdsMg*` ABID
functions and `tools/mgprobe` speak to a remote `openads_serverd`.

- `MgConnect`: `[bytes server]` — the `host:port` (or drive path)
  passed to `AdsMgConnect`. `MgConnectAck` is empty on success;
  the handshake doubles as an eager reachability probe.
- `MgRequest`: `[u8 kind][u16 arg]` — always 3 bytes. `kind`:
  `0x01` Snapshot (full `MgSnapshot`, covers every `Get*`),
  `0x02` KillUser (`arg` = connection number), `0x03`
  ResetCommStats, `0x04` DumpTables.
- `MgReplyAck`: a fully serialized `MgSnapshot`, little-endian —
  live counts (connections / work areas / tables / users /
  worker threads), per-entity lists, process RSS, listener port,
  and the cumulative `MgStats` (uptime, comm packet totals,
  server-initiated disconnects, high-water marks).
- An unknown `MgRequestKind` is answered with `Error` (`0xFF`).

## 6. Versioning

- This spec covers OpenADS **v1.0.0-rc25**. Bumps append new
  opcodes and document them here without breaking existing ones.
- Clients can probe the server version via `Hello` → the banner
  string is `openads/<semver>`.

## 7. Error handling expectations

- Any frame may be replied with `Error` (`0xFF`). Clients must
  parse the 4-byte ACE-code prefix before treating the rest as
  message text.
- A **peer-closed connection** mid-frame is treated as
  `AE_INTERNAL_ERROR` 5000 with message `peer closed connection`
  — the wire layer bubbles this up via `recv_exact`.
- `AE_FUNCTION_NOT_AVAILABLE` 5004 from `Connect` means the URI
  scheme isn't supported, or the server was built without the
  matching transport (rare since TLS shipped in v0.4.0).

## 8. Reference impls

- **Server**: `src/network/server.{h,cpp}` plus the standalone
  `tools/serverd/openads_serverd` CLI.
- **Client**: `src/network/client.{h,cpp}` (`RemoteConnection`)
  + the dual-mode dispatch in `src/abi/ace_exports.cpp`'s
  `AdsConnect60` for the public `tcp://` / `tls://` URI
  integration.
- **Transport abstraction**: `src/network/transport.h` defines
  the `ITransport` polymorphic surface (M12.13). Concrete impls:
  `PlainTransport` (TCP) and `TlsTransport` (mbedtls, vendored
  statically since v1.0.0-rc8).
- **Wire codec**: `src/network/wire.{h,cpp}` (frame
  encode / decode + `Opcode` enum).
