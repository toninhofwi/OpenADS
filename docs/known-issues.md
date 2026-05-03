# Known issues — M3 / M3.5

A code review on 2026-05-03 surfaced compatibility-breaking deviations
from the FoxPro CDX / Clipper NTX on-disk specs. The implementations
round-trip *their own* output but cannot interoperate with `.cdx` /
`.ntx` files produced by other tools, which contradicts the
README "Validation" goal of byte-level compatibility.

## Status — M3.6 / M3.7

| # | Issue | Status |
|---|-------|--------|
| 1 | CDX leaf entry bit width hardcoded | **Fixed** (`c1b8cf8`). Encoder uses Harbour-equivalent `compute_layout` (bBits derived from key length). |
| 3 | CDX branch descent wrong endian / offset | **Fixed** (`7f3041f`). `seek_first` now reads child as BE per `hb_cdxPageGetKeyPage`. |
| 5 | NTX `insert` returns AE_FUNCTION_NOT_AVAILABLE on second page | **Fixed for single-level** (`6ab97c4`). Root-leaf overflow now creates a branch root with two leaves. Multi-level recursion still pending. |
| 6 | AdsOpenIndex lifecycle race | **Fixed** (`efe8d22`). |
| 7 | AdsCreateIndex indexes deleted records | **Fixed** (`efe8d22`). |
| 10 | NTX erase ignores recno when recno=0 | **Confirmed-as-spec**. Matches Harbour's `NTX_IGNORE_REC_NUM = 0x0UL` convention (passing recno=0 means "any recno with this key"). |
| 11 | NTX soft seek past end | **Confirmed-as-correct**. The seek loop already descends the trailing right child when `i >= kc` on a non-leaf path; soft fallback only trips when reaching a leaf, which lands on the last key with `AfterKey`. |
| 12 | Descending / unique flag round-trip untested | **Fixed** (`bb75c22`). |
| 4  | NTX multi-level `next` / `prev` correctness | **Fixed** (`aaf8f52`, M3.8). Cache-based in-order traversal + leaf-split fix (separator promoted, not duplicated). |

Item 2 (CDX compound structure tag) remains open and lands as **M3.9**.

- **#2** needs writing the compound structure tag B+tree at page 0 (FoxPro on-disk layout: structure tag root → leaves whose entries map tag-name strings to per-tag CDXTAGHEADER pages). The current single-tag flat layout in `CdxIndex::create` would need to be replaced, plus `open()` learning to walk the structure tag. Recipe: page 0 = structure tag CDXTAGHEADER, page 1 = its leaf with one entry `(tag_name[key_len], 0[4], sub_header_page[4])` (internal-node-style entries, NOT compact-encoded), page 2 = sub-tag CDXTAGHEADER, page 3+ = sub-tag's compact-leaf B+tree.
- **#4 (closed)** was solved via a cache-based traversal: `ensure_cache_()` walks the tree depth-first into a flat `std::vector<CachedKey>`; `seek_first` / `seek_last` / `seek_key` / `next` / `prev` operate on the vector by index. Write paths (`insert` / `erase`) keep stack-based descent via `seek_key_for_write_` and mark `cache_dirty_ = true`. A separate split-bug fix (`Entry separator = all[mid]; right_half = [mid+1, end)`) prevents the separator from appearing twice during multi-level walks.

CDX files are still single-tag flat (non-FoxPro) and cannot be read by FoxPro tools until M3.9 lands.

Items 8, 9, 13, 14, 15, 16, 17 are minor / hygiene and tracked below.

## Critical (compat-breaking)

1. **CDX leaf entry bit width is hardcoded.** `src/drivers/cdx/cdx_index.cpp:163-167` always writes `recBits=24, dupBits=8, trlBits=8` (5 bytes per entry). FoxPro derives these dynamically: `bBits = ceil(log2(keylen+1))`, `dupBits = trlBits = bBits`, total `keyBytes ∈ {3,4,5}`. For `keylen=4` real layout is 18/3/3 bits packed in 3 bytes.
2. **CDX tag name stashed in `reserved2`.** `src/drivers/cdx/cdx_index.cpp:117-120, 571-573` writes the tag name at offset 24 of `CDXTAGHEADER`, which corrupts the FoxPro `reserved2[68]`. Real FoxPro CDX is compound: page 0 is the structure tag; sub-tag names live as keys in the structure tag's B+tree, pointing at sub-tag header pages. Single-tag flat layout is non-standard.
3. **CDX branch descent uses wrong offset and endianness.** `src/drivers/cdx/cdx_index.cpp:310-316` reads `LE_UINT32(base + 12 + key_size + 4)` for the leftmost child. Per Harbour `dbfcdx1.c:1554-1556`, the child page is stored **big-endian** at `(iKey+1) * (key_size+8) - 4`. Multi-level CDX trees are unreachable.
4. **NTX `seek_key` / `next` / `prev` semantics are wrong on multi-level trees.** `src/drivers/ntx/ntx_index.cpp:248-258` returns `Exact` on an internal-node match without descending, then `next()` re-descends the right subtree → keys can be revisited.
5. **NTX `insert` returns `AE_FUNCTION_NOT_AVAILABLE` on the second page.** `src/drivers/ntx/ntx_index.cpp:424-426`. The plan and the README "M3 Done" row claim full insert with split.
6. **`AdsOpenIndex` lifecycle race.** `src/abi/ace_exports.cpp:464-490` calls `Table::set_order(...)` which destroys any previous order via `std::optional::emplace`, leaving the prior index handle dangling in `index_bindings()`. A subsequent `AdsCloseIndex(stale)` then clears the live order.
7. **`AdsCreateIndex` indexes deleted records.** `src/abi/ace_exports.cpp:541-549` walks every recno via `goto_record` without checking `is_deleted()`. The resulting index produces phantom recnos.

## Important (incomplete coverage)

8. CDX `seek_key` always re-descends from `seek_first` — O(N) over the leaf chain instead of O(log N) via root descent.
9. CDX `insert` mutates `file_size_` only in memory; a freshly created+inserted CDX read by another process before `flush()` returns garbage.
10. NTX `erase` ignores `recno` when `recno == 0` but enforces it otherwise — opposite of Harbour `hb_ntxPageKeyDel` semantics.
11. NTX soft seek past end leaves the cursor empty when `i ≥ kc` on a non-leaf path; should land on the last key with `AfterKey`.
12. No tests round-trip `descending=true` or `unique=true` flags through reopen; descending order is silently broken.
13. CDX test fixtures are all "create→insert→reopen" — zero coverage of real FoxPro byte sequences.

## Minor

14. `src/drivers/cdx/cdx_index.cpp:283` silently writes `freeSpc=0` when entries collide with suffix bytes; should error.
15. NTX `format_empty_page` doesn't reuse freed offset slots — divergent from Harbour but tolerable.
16. `src/engine/order.cpp` is the placeholder line; class is header-only — collapse into header or move bodies.
17. `AdsSetIndexDirection` returns `5004`; `descend_` is already on the index and could trivially be flipped on a follow-up.

## M3.6 plan (TBD)

A dedicated milestone will:

- Replace CDX bit-packing with the FoxPro-derived `bBits` formula (cite Harbour `hb_cdxPageLeafInitSpace`).
- Implement the compound CDX layout: structure tag at page 0, sub-tag headers reachable via the structure tag B+tree.
- Fix CDX branch descent (BE child pointers at the correct offset).
- Land the NTX leaf split and revisit descent / next / prev for multi-level trees.
- Tighten the `AdsOpenIndex` / `AdsCloseIndex` lifecycle so installing a new order does not orphan prior bindings.
- Skip deleted records in `AdsCreateIndex`.
- Add tests: descending flag round-trip, unique flag round-trip, soft-seek past-end, branch descent, multi-leaf walks, and at least one fixture file produced by Harbour `dbfcdx` so OpenADS proves it can read a real FoxPro `.cdx`.

Until M3.6 lands, the index path is functional **only against indexes
created by OpenADS itself.** For interoperability with applications
that already have `.cdx` / `.ntx` files on disk, defer to M3.6.
