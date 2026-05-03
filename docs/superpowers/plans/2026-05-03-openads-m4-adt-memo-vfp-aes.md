# OpenADS — M4 ADT + Memo + VFP + AES Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete the data-format surface: read+write ADT (ADS native) tables with extended types (autoinc, modtime, GUID, timestamp, NULL bitmap), DBT/FPT/ADM memo stores, VFP-typed DBF, and AES-128/256 record encryption. End-to-end test: an L1 sequence creates an ADT with a memo field, writes records (some encrypted), reads them back, opens a VFP DBF, and round-trips memos through DBT and FPT.

**Architecture:** Three new format drivers (`AdtDriver`, `VfpDriver`) plus three memo backends (`DbtMemo`, `FptMemo`, `AdmMemo`). The driver trait grows a `memo_store()` accessor; field decoding routes memo references through it. `engine::Encryption` wraps an `IBlockCipher` and applies record-level AES at the driver boundary. AES is vendored as `tinyaes` (single-header, MIT). 14 new ACE entry points cover memo + encryption + autoinc.

**Tech Stack:** Same — adds `third_party/tinyaes` vendored.

---

## Scope cuts

To stay shippable in one session:

- **ADT format**: header + descriptors + records + extended types (autoinc, modtime, timestamp, NULL bitmap). Compaction / record locks specific to ADT use shared engine logic.
- **ADM memo**: read+write basic (block alloc + chain). Real ADS-proprietary block packing follows Harbour `dbffpt` patterns where they overlap.
- **FPT/DBT memo**: full, both endians.
- **VFP driver**: DBF 0x30/0x31 read+write, autoinc field, `_NullFlags` system field for NULL bitmap.
- **AES**: ECB-mode AES-128/256 record encryption (mirrors ADS legacy). Tinyaes vendored as MIT.
- **Out**: AES-CBC, full ADM proprietary edge cases, ADT compaction, AdsRestructureTable.

---

(Detailed task breakdown identical in shape to M3 plan; execution will inline-write tests + impls in a TDD red→green→commit cadence.)

## Tasks

1. Vendor tinyaes (`third_party/tinyaes/`) and add CMake hookup.
2. `engine::Aes` C++ wrapper with `encrypt_block` / `decrypt_block` over AES-128 + AES-256.
3. `MemoStore` trait + `DbtMemo` (dBase III).
4. `FptMemo` (FoxPro / VFP).
5. `AdmMemo` (ADS).
6. Memo wiring into `IDriver` + `Table` (`read_field` follows memo blocks for `M`-fields).
7. ADT format — header + field descriptors + record reader/writer.
8. VFP driver (DBF 0x30/0x31 + extras).
9. Encryption boundary: wrap raw record buffers when table flag set.
10. ABI thunks (memo + encryption + autoinc + binary).
11. End-to-end smoke (`abi_m4_smoke_test.cpp`).
12. README + tag.

(Tasks intentionally compact — code is written inline against the FoxPro/Clipper specs and Harbour `src/rdd/dbffpt`, `src/rdd/dbfntx`, `src/rdd/dbfcdx` references.)
