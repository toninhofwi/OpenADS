#pragma once

// M-AOF.2 — evaluate an AOF AST against a Table and produce a
// per-record bitmap. The bitmap drives M-AOF.3 (Skip / GoTop honour
// the bitmap and AdsGetAOFOptLevel reports FULL / PART / NONE).
//
// V1 implementation walks every record once and evaluates the AST
// against the record's decoded field values. That gives us a
// correctness baseline end-to-end before we add index-accelerated
// leaf evaluation in M-AOF.4 — the index path is pure perf, the
// AST → bitmap contract stays identical.
//
// Provenance / clean-room. Same constraints as aof_expr.{h,cpp}: no
// GPL Harbour `contrib/rddbm/bmdbfx.c` internals were read while
// authoring this file. Only its public symbol list (`rddbm.hbx`)
// was consulted.

#include "engine/aof_expr.h"
#include "util/result.h"

#include <cstdint>
#include <vector>

namespace openads::engine {

class Table;

namespace aof {

// One bit per record: bitmap[recno - 1] == true means the record
// passes the filter. Size always equals record_count(); for an
// empty table the bitmap is empty and every Skip / GoTop call
// short-circuits to EoF.
using Bitmap = std::vector<bool>;

// AdsGetAOFOptLevel mirror — how completely the AOF expression was
// answered through index range scans rather than a full table walk.
enum class OptLevel {
    None = 0,           // ADS_OPTIMIZED_NONE — no leaf served by index
    Part = 1,           // ADS_OPTIMIZED_PART — some leaves served, others scanned
    Full = 2,           // ADS_OPTIMIZED_FULL — every leaf served by an index
};

struct EvalReport {
    Bitmap   bm;
    OptLevel level = OptLevel::None;
};

// Evaluate `n` against every record of `t` and produce the bitmap.
// Reads the current cursor and walks all recnos in [1, count()];
// the table position on return is unspecified — callers wanting to
// keep their position must save and restore it themselves.
util::Result<Bitmap> evaluate(const Node& n, Table& t);

// Evaluate `n` against the table's current cursor position only.
// The table is not repositioned; read_field() is called as-is.
bool evaluate_record(const Node& n, Table& t);

// M-AOF.4 — same contract as evaluate(), but routes individual
// leaves through CDX / NTX index range scans whenever an open
// index's key expression matches the leaf's field. Reports the
// resulting OptLevel so AdsGetAOFOptLevel can surface the
// FULL / PART / NONE flavour to the caller.
util::Result<EvalReport> evaluate_optimised(const Node& n, Table& t);

} // namespace aof
} // namespace openads::engine
