#pragma once

#include "drivers/dbf_common.h"
#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::engine {

class Table;

// Evaluate a CDX-style index key expression against the current
// record buffer of `t`, padded / clipped to `key_len` bytes.
//
// Supported grammar (subset of Clipper / FoxPro key expressions):
//
//   expr  := term ('+' term)*
//   term  := call | ident | number | string-literal | '(' expr ')'
//   call  := ident '(' (expr (',' expr)*)? ')'
//
//   functions:  UPPER(s) LOWER(s) LTRIM(s) RTRIM(s) ALLTRIM(s)
//               STR(n)   STR(n, len)   STR(n, len, dec)
//               DTOS(d)
//               SUBSTR(s, start)   SUBSTR(s, start, len)
//
// Identifiers are looked up first as field names against the table's
// schema; an unknown identifier evaluates to an empty string. Bare
// field names short-circuit to the legacy "raw bytes padded with
// spaces" path so existing single-field expressions stay byte-exact
// with M8.8.
util::Result<std::string>
    evaluate_index_expr(Table&             t,
                        const std::string& expr,
                        std::uint16_t      key_len);

// FOR-clause / scope predicate evaluator. Parses a Clipper-style
// boolean expression and returns whether the current row passes.
// On parse failure or unsupported syntax returns true (be permissive
// — the FOR clause then degrades to "include all").
bool evaluate_index_expr_truthy(Table& t, const std::string& expr);

// Drop `ALIAS->` workarea qualifiers from a key/FOR expression so a
// bare `FIELD->NAME` resolves to the plain field name `NAME`. Used by
// the evaluator (write side) and by the ABI seek path so a numeric
// seek can recover the field width from the schema the same way the
// stored key was built. Compound expressions keep their structure;
// only the qualifier is removed (e.g. `UPPER(CUST->NAME)` -> `UPPER(NAME)`).
std::string strip_alias_qualifiers(const std::string& expr);

} // namespace openads::engine
