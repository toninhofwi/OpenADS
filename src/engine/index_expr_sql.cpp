#include "engine/index_expr.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace openads::engine {

// ── Tier-2 SQL push-down (spike) ──────────────────────────────────────────
//
// A recursive-descent translator that mirrors the grammar of the FOR-clause
// interpreter (evaluate_index_expr_truthy in index_expr.cpp) but, instead of
// evaluating against a row, EMITS a SQL boolean expression. Anything it cannot
// translate sets `ok = false`; the public entry then returns std::nullopt and
// the caller falls back to the row interpreter — push-down stays optional and
// can never produce a predicate that matches the wrong rows.
//
//   or   := and ('.OR.'  and)*
//   and  := not ('.AND.' not)*
//   not  := ('.NOT.' | '!') not | cmp
//   cmp  := '(' or ')' | value (op value)?
//   value:= term ('+' term)*
//   term := string | number | ident | ident '(' args ')'

namespace {

struct Emitter {
    const std::string& s;
    std::size_t        i = 0;
    const SqlDialect&  d;
    bool               ok = true;

    Emitter(const std::string& src, const SqlDialect& dia) : s(src), d(dia) {}

    void skip_ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool peek_kw(const char* kw) {
        skip_ws();
        std::size_t L = std::strlen(kw);
        if (i + L > s.size()) return false;
        for (std::size_t k = 0; k < L; ++k) {
            if (std::toupper(static_cast<unsigned char>(s[i + k])) !=
                static_cast<unsigned char>(kw[k]))
                return false;
        }
        return true;
    }
    bool match_kw(const char* kw) {
        if (!peek_kw(kw)) return false;
        i += std::strlen(kw);
        return true;
    }
    bool match_char(char c) {
        skip_ws();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }

    std::string emit_or() {
        std::string a = emit_and();
        while (ok && match_kw(".OR.")) {
            std::string b = emit_and();
            a = "(" + a + " OR " + b + ")";
        }
        return a;
    }

    std::string emit_and() {
        std::string a = emit_not();
        while (ok && match_kw(".AND.")) {
            std::string b = emit_not();
            a = "(" + a + " AND " + b + ")";
        }
        return a;
    }

    std::string emit_not() {
        skip_ws();
        if (match_kw(".NOT.") || match_char('!')) {
            std::string inner = emit_not();
            return "(NOT " + inner + ")";
        }
        return emit_cmp();
    }

    std::string emit_cmp() {
        skip_ws();
        if (match_char('(')) {
            std::string inner = emit_or();
            if (!match_char(')')) ok = false;
            return inner;
        }
        std::string lhs = emit_value();
        skip_ws();
        std::string op;
        auto try_op = [&](const char* tok, const char* sqlop) {
            std::size_t L = std::strlen(tok);
            if (i + L > s.size()) return false;
            if (std::memcmp(s.data() + i, tok, L) != 0) return false;
            i += L;
            op = sqlop;
            return true;
        };
        // Longest match first so ">=" beats ">", "==" beats "=", etc.
        if (try_op("==", "=")  || try_op("!=", "<>") || try_op("<>", "<>") ||
            try_op(">=", ">=") || try_op("<=", "<=") ||
            try_op("=", "=")   || try_op(">", ">")   || try_op("<", "<")   ||
            try_op("#", "<>")) {
            std::string rhs = emit_value();
            return lhs + " " + op + " " + rhs;
        }
        // A bare scalar in boolean position has no portable SQL coercion —
        // decline and let the interpreter handle the whole predicate.
        ok = false;
        return "";
    }

    std::string emit_value() {
        std::string a = emit_term();
        while (ok) {
            skip_ws();
            if (i < s.size() && s[i] == '+') {
                ++i;
                std::string b = emit_term();
                if (d.use_concat_fn)
                    a = "CONCAT(" + a + ", " + b + ")";
                else
                    a = "(" + a + " " + d.concat_op + " " + b + ")";
            } else {
                break;
            }
        }
        return a;
    }

    std::string emit_term() {
        skip_ws();
        if (i >= s.size()) { ok = false; return ""; }
        char c = s[i];

        // String literal — re-quote SQL-style, doubling embedded single quotes.
        if (c == '"' || c == '\'') {
            char q = c; ++i;
            std::string lit;
            while (i < s.size() && s[i] != q) lit.push_back(s[i++]);
            if (i < s.size()) ++i;
            std::string out = "'";
            for (char ch : lit) {
                if (ch == '\'') out += "''";
                else            out.push_back(ch);
            }
            out += "'";
            return out;
        }

        // Number literal (with optional leading minus).
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            std::string num;
            if (c == '-') num.push_back(s[i++]);
            while (i < s.size() &&
                   (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
                num.push_back(s[i++]);
            return num;
        }

        // Identifier → function call or bare column.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string name;
            while (i < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
                name.push_back(s[i++]);
            skip_ws();
            if (i < s.size() && s[i] == '(') {
                ++i;
                std::vector<std::string> args;
                skip_ws();
                if (!(i < s.size() && s[i] == ')')) {
                    args.push_back(emit_value());
                    while (ok && match_char(',')) args.push_back(emit_value());
                }
                if (!match_char(')')) ok = false;
                if (!ok) return "";
                std::string up = name;
                for (auto& ch : up)
                    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                return emit_func(up, args);
            }
            // Bare field name → SQL column reference, verbatim.
            return name;
        }

        ok = false;
        return "";
    }

    std::string emit_func(const std::string& fn,
                          const std::vector<std::string>& args) {
        auto arity = [&](std::size_t n) {
            if (args.size() < n) { ok = false; return false; }
            return true;
        };
        if (fn == "UPPER" && arity(1)) return "UPPER(" + args[0] + ")";
        if (fn == "LOWER" && arity(1)) return "LOWER(" + args[0] + ")";
        if (fn == "LTRIM" && arity(1)) return "LTRIM(" + args[0] + ")";
        if ((fn == "RTRIM" || fn == "TRIM") && arity(1))
            return "RTRIM(" + args[0] + ")";
        if (fn == "ALLTRIM" && arity(1))
            return d.alltrim_open + args[0] + d.alltrim_close;
        if ((fn == "SUBSTR" || fn == "SUBS") && args.size() >= 2) {
            std::string r = d.substr_fn + "(" + args[0] + ", " + args[1];
            if (args.size() >= 3) r += ", " + args[2];
            return r + ")";
        }
        // RECNO / DELETED / STR / VAL / DTOS / CTOD / IIF / unknown → decline.
        ok = false;
        return "";
    }
};

} // namespace

std::optional<std::string>
try_emit_sql_where(const std::string& expr, const SqlDialect& dialect) {
    std::string e = strip_alias_qualifiers(expr);
    Emitter em(e, dialect);
    em.skip_ws();
    if (em.i >= e.size()) return std::nullopt;   // empty / whitespace only
    std::string out = em.emit_or();
    em.skip_ws();
    // Unconsumed input means an operator/construct we don't model → decline,
    // so we never emit a predicate that silently drops part of the filter.
    if (!em.ok || em.i != e.size()) return std::nullopt;
    return out;
}

} // namespace openads::engine
