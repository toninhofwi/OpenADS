#include "engine/index_expr.h"

#include "engine/table.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <variant>

namespace openads::engine {

namespace {

// ---- Tokenizer ------------------------------------------------------

enum class TokKind {
    Ident, Number, String, LParen, RParen, Comma, Plus, End
};

struct Token {
    TokKind     kind;
    std::string text;
};

std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '(') { out.push_back({TokKind::LParen, "("}); ++i; continue; }
        if (c == ')') { out.push_back({TokKind::RParen, ")"}); ++i; continue; }
        if (c == ',') { out.push_back({TokKind::Comma,  ","}); ++i; continue; }
        if (c == '+') { out.push_back({TokKind::Plus,   "+"}); ++i; continue; }
        if (c == '"' || c == '\'') {
            char q = c; ++i;
            std::string lit;
            while (i < s.size() && s[i] != q) { lit.push_back(s[i++]); }
            if (i < s.size()) ++i;
            out.push_back({TokKind::String, std::move(lit)});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            std::string num;
            if (c == '-') { num.push_back(c); ++i; }
            while (i < s.size() &&
                   (std::isdigit(static_cast<unsigned char>(s[i])) ||
                    s[i] == '.')) {
                num.push_back(s[i++]);
            }
            out.push_back({TokKind::Number, std::move(num)});
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string id;
            while (i < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[i])) ||
                    s[i] == '_')) {
                id.push_back(s[i++]);
            }
            out.push_back({TokKind::Ident, std::move(id)});
            continue;
        }
        ++i;   // skip the unknown char rather than die — best-effort.
    }
    out.push_back({TokKind::End, ""});
    return out;
}

// Harbour key / FOR expressions arrive with the workarea alias still
// attached — `INDEX ON CUST->NAME` reaches the RDD as the literal
// text "CUST->NAME". An index belongs to exactly one table, so an
// `ALIAS->FIELD` qualifier always denotes a field of that table;
// drop the `ALIAS->` so the rest of the evaluator sees a plain field
// reference. (The tokenizer otherwise discards `-` and `>` as stray
// punctuation, leaving the alias to parse as an unknown identifier —
// every key then evaluates blank and the index degenerates to
// record order.) Works inside calls too: UPPER(CUST->NAME).
std::string strip_alias_qualifiers(const std::string& expr) {
    std::string out;
    std::size_t i = 0;
    while (i < expr.size()) {
        unsigned char c = static_cast<unsigned char>(expr[i]);
        if (std::isalpha(c) || c == '_') {
            std::size_t j = i;
            while (j < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[j])) ||
                    expr[j] == '_')) {
                ++j;
            }
            std::size_t k = j;
            while (k < expr.size() &&
                   std::isspace(static_cast<unsigned char>(expr[k]))) {
                ++k;
            }
            if (k + 1 < expr.size() && expr[k] == '-' && expr[k + 1] == '>') {
                i = k + 2;          // discard `<ident> ->`
                continue;
            }
            out.append(expr, i, j - i);
            i = j;
            continue;
        }
        out.push_back(expr[i]);
        ++i;
    }
    return out;
}

// ---- AST + evaluator ------------------------------------------------

struct Value {
    bool        is_number = false;
    std::string s;
    double      n = 0.0;
};

// ---- UTF-8 codepoint walker (M9.22) --------------------------------------
//
// UPPER / LOWER / SUBSTR walk codepoints instead of bytes so an index
// expression like UPPER(name) over a UTF-8 column normalises non-ASCII
// characters (ñ → Ñ, ç → Ç, é → É, …) instead of leaving the multi-byte
// sequence unchanged. The case-mapping table covers ASCII + Latin-1
// supplement + the U+00FF / U+0178 pair; codepoints outside that range
// pass through untouched (full Unicode case folding is ICU territory
// and out of scope until the engine carries an ICU dependency).

std::uint32_t utf8_decode_at(const std::string& s, std::size_t& i) {
    if (i >= s.size()) return 0xFFFD;
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::uint32_t cp = 0xFFFD;
    std::size_t adv = 1;
    auto byte = [&](std::size_t k) {
        return static_cast<unsigned char>(s[k]);
    };
    if (c < 0x80) {
        cp = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() &&
               (byte(i + 1) & 0xC0) == 0x80) {
        cp = (static_cast<std::uint32_t>(c & 0x1F) << 6) |
              static_cast<std::uint32_t>(byte(i + 1) & 0x3F);
        adv = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() &&
               (byte(i + 1) & 0xC0) == 0x80 &&
               (byte(i + 2) & 0xC0) == 0x80) {
        cp = (static_cast<std::uint32_t>(c & 0x0F) << 12) |
             (static_cast<std::uint32_t>(byte(i + 1) & 0x3F) << 6) |
              static_cast<std::uint32_t>(byte(i + 2) & 0x3F);
        adv = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size() &&
               (byte(i + 1) & 0xC0) == 0x80 &&
               (byte(i + 2) & 0xC0) == 0x80 &&
               (byte(i + 3) & 0xC0) == 0x80) {
        cp = (static_cast<std::uint32_t>(c & 0x07) << 18) |
             (static_cast<std::uint32_t>(byte(i + 1) & 0x3F) << 12) |
             (static_cast<std::uint32_t>(byte(i + 2) & 0x3F) << 6) |
              static_cast<std::uint32_t>(byte(i + 3) & 0x3F);
        adv = 4;
    }
    i += adv;
    return cp;
}

void utf8_encode(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

std::uint32_t to_upper_cp(std::uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 0x20;
    // Latin-1 supplement: à..þ → À..Þ, skipping ÷ at 0xF7.
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
    if (cp == 0xFF) return 0x178;   // ÿ → Ÿ
    return cp;
}

std::uint32_t to_lower_cp(std::uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 0x20;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
    if (cp == 0x178) return 0xFF;
    return cp;
}

std::string upper_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        std::uint32_t cp = utf8_decode_at(s, i);
        utf8_encode(out, to_upper_cp(cp));
    }
    return out;
}

std::string lower_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        std::uint32_t cp = utf8_decode_at(s, i);
        utf8_encode(out, to_lower_cp(cp));
    }
    return out;
}

std::string substr_utf8(const std::string& s, int start_1based, int len) {
    if (start_1based < 1) start_1based = 1;
    if (len < 0) len = 0;
    std::string out;
    std::size_t i = 0;
    int char_index = 0;       // 0-based codepoint counter
    int copied = 0;
    while (i < s.size() && copied < len) {
        std::size_t before = i;
        (void)utf8_decode_at(s, i);   // advance one codepoint
        if (char_index + 1 >= start_1based) {
            out.append(s, before, i - before);
            ++copied;
        }
        ++char_index;
        if (i == before) break;   // safety on malformed input
    }
    return out;
}

std::string ltrim_ascii(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}
std::string rtrim_ascii(const std::string& s) {
    std::size_t e = s.size();
    while (e > 0 && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(0, e);
}

class Parser {
public:
    Parser(const std::vector<Token>& toks, Table& t)
        : toks_(toks), t_(t) {}

    Value parse_expr() {
        Value lhs = parse_term();
        while (peek().kind == TokKind::Plus) {
            ++pos_;
            Value rhs = parse_term();
            // String concatenation; numeric values are stringified
            // first (Clipper semantics for + on mixed operands are
            // unsupported in CDX expressions).
            std::string a = lhs.is_number ? format_number(lhs.n, 0, 0) : lhs.s;
            std::string b = rhs.is_number ? format_number(rhs.n, 0, 0) : rhs.s;
            lhs.is_number = false;
            lhs.s = a + b;
        }
        return lhs;
    }

private:
    const Token& peek() const { return toks_[pos_]; }

    Value parse_term() {
        const Token& tk = peek();
        if (tk.kind == TokKind::String) {
            ++pos_;
            Value v;
            v.is_number = false;
            v.s = tk.text;
            return v;
        }
        if (tk.kind == TokKind::Number) {
            ++pos_;
            Value v;
            v.is_number = true;
            v.n = std::strtod(tk.text.c_str(), nullptr);
            return v;
        }
        if (tk.kind == TokKind::LParen) {
            ++pos_;
            Value v = parse_expr();
            if (peek().kind == TokKind::RParen) ++pos_;
            return v;
        }
        if (tk.kind == TokKind::Ident) {
            std::string name = tk.text;
            ++pos_;
            if (peek().kind == TokKind::LParen) {
                ++pos_;
                std::vector<Value> args;
                if (peek().kind != TokKind::RParen) {
                    args.push_back(parse_expr());
                    while (peek().kind == TokKind::Comma) {
                        ++pos_;
                        args.push_back(parse_expr());
                    }
                }
                if (peek().kind == TokKind::RParen) ++pos_;
                return call(upper_utf8(name), args);
            }
            return field_or_empty(name);
        }
        Value v;
        v.is_number = false;
        return v;
    }

    Value field_or_empty(const std::string& name) {
        Value v;
        v.is_number = false;
        std::int32_t fidx = t_.field_index(name);
        if (fidx < 0) return v;
        auto r = t_.read_field(static_cast<std::uint16_t>(fidx));
        if (!r) return v;
        const auto& f = t_.field_descriptor(static_cast<std::uint16_t>(fidx));
        if (f.type == drivers::DbfFieldType::Numeric ||
            f.type == drivers::DbfFieldType::Float ||
            f.type == drivers::DbfFieldType::Integer ||
            f.type == drivers::DbfFieldType::Double ||
            f.type == drivers::DbfFieldType::Currency ||
            f.type == drivers::DbfFieldType::AdtMoney) {
            v.is_number = true;
            v.n = r.value().as_double;
            // For arithmetic-style indexes the Clipper convention is
            // STR(numeric, len). The bare-field path returns the
            // *string* representation matching the on-disk bytes.
            v.s = r.value().as_string;
        } else {
            v.is_number = false;
            v.s = r.value().as_string;
        }
        return v;
    }

    static std::string format_number(double v, int width, int dec) {
        char buf[64];
        if (width <= 0) {
            std::snprintf(buf, sizeof(buf), "%.*f", dec > 0 ? dec : 0, v);
        } else {
            std::snprintf(buf, sizeof(buf), "%*.*f", width,
                          dec > 0 ? dec : 0, v);
        }
        std::string out = buf;
        if (width > 0 && static_cast<int>(out.size()) > width) {
            out.resize(static_cast<std::size_t>(width));
        }
        return out;
    }

    Value call(const std::string& fn, const std::vector<Value>& args) {
        Value v;
        v.is_number = false;
        if (fn == "UPPER" && args.size() >= 1) {
            v.s = upper_utf8(args[0].s);
        } else if (fn == "LOWER" && args.size() >= 1) {
            v.s = lower_utf8(args[0].s);
        } else if (fn == "LTRIM" && args.size() >= 1) {
            v.s = ltrim_ascii(args[0].s);
        } else if (fn == "RTRIM" && args.size() >= 1) {
            v.s = rtrim_ascii(args[0].s);
        } else if (fn == "ALLTRIM" && args.size() >= 1) {
            v.s = rtrim_ascii(ltrim_ascii(args[0].s));
        } else if (fn == "DTOS" && args.size() >= 1) {
            // Date fields are already YYYYMMDD on disk; just return the raw bytes.
            v.s = args[0].s;
        } else if (fn == "STR") {
            int width = args.size() >= 2 && args[1].is_number
                      ? static_cast<int>(args[1].n) : 10;
            int dec   = args.size() >= 3 && args[2].is_number
                      ? static_cast<int>(args[2].n) : 0;
            double n = args[0].is_number ? args[0].n
                     : std::strtod(args[0].s.c_str(), nullptr);
            v.s = format_number(n, width, dec);
        } else if (fn == "SUBSTR" && args.size() >= 2) {
            const std::string& s = args[0].s;
            int start = args[1].is_number ? static_cast<int>(args[1].n) : 1;
            int len   = args.size() >= 3 && args[2].is_number
                      ? static_cast<int>(args[2].n)
                      : static_cast<int>(s.size());
            v.s = substr_utf8(s, start, len);
        } else {
            // Unknown function — return empty string.
        }
        return v;
    }

    const std::vector<Token>& toks_;
    std::size_t               pos_ = 0;
    Table&                    t_;
};

} // namespace

util::Result<std::string>
evaluate_index_expr(Table& t, const std::string& expr, std::uint16_t key_len) {
    const std::string e = strip_alias_qualifiers(expr);
    if (e.empty()) return std::string(key_len, ' ');

    // Fast-path: bare field-name expression (matches the M8.8 behaviour
    // exactly so existing CDX files round-trip identical bytes).
    if (std::all_of(e.begin(), e.end(),
            [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            })) {
        // Only treat as bare-field when the identifier resolves to a
        // real column; otherwise fall through to the general parser.
        if (t.field_index(e) >= 0) {
            std::int32_t fidx = t.field_index(e);
            const auto& f = t.field_descriptor(static_cast<std::uint16_t>(fidx));
            auto r = t.read_field(static_cast<std::uint16_t>(fidx));
            if (!r) return r.error();
            std::string key = r.value().as_string;
            if (key.size() < key_len) key.append(key_len - key.size(), ' ');
            if (key.size() > key_len) key.resize(key_len);
            (void)f;
            return key;
        }
    }

    auto toks = tokenize(e);
    Parser p(toks, t);
    Value v = p.parse_expr();
    std::string s = v.s;
    if (s.size() < key_len) s.append(key_len - s.size(), ' ');
    if (s.size() > key_len) s.resize(key_len);
    return s;
}

// ---- M(rddads-compat) FOR-clause / scope truthy evaluator ----------------
//
// A tiny recursive-descent parser sufficient for the predicate forms
// rddtst.prg + most Clipper apps emit:
//
//   FNUM > 2 .AND. FNUM <= 4
//   FSTR == "3"
//   FNUM != 2 .AND. FNUM < 4
//   RECNO() <> 5
//
// Anything outside the supported grammar evaluates to TRUE (so the
// FOR clause degrades to "include every row" rather than corrupting
// the index — callers can still use it, just without the filter).
namespace {

struct Lex {
    const std::string& s;
    std::size_t i = 0;
    explicit Lex(const std::string& src) : s(src) {}

    void skip_ws() {
        while (i < s.size() && std::isspace(
                static_cast<unsigned char>(s[i]))) ++i;
    }
    bool eof() { skip_ws(); return i >= s.size(); }
    bool peek_kw(const char* kw) {
        skip_ws();
        std::size_t L = std::strlen(kw);
        if (i + L > s.size()) return false;
        for (std::size_t k = 0; k < L; ++k) {
            char a = static_cast<char>(std::toupper(
                static_cast<unsigned char>(s[i + k])));
            if (a != kw[k]) return false;
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
};

bool eval_or(Lex& lx, Table& t);
bool eval_and(Lex& lx, Table& t);
bool eval_not(Lex& lx, Table& t);
bool eval_cmp(Lex& lx, Table& t);

Value parse_atom(Lex& lx, Table& t) {
    lx.skip_ws();
    Value v;
    if (lx.i < lx.s.size() &&
        (lx.s[lx.i] == '"' || lx.s[lx.i] == '\'')) {
        char q = lx.s[lx.i++];
        std::string lit;
        while (lx.i < lx.s.size() && lx.s[lx.i] != q) {
            lit.push_back(lx.s[lx.i++]);
        }
        if (lx.i < lx.s.size() && lx.s[lx.i] == q) ++lx.i;
        v.is_number = false; v.s = lit;
        return v;
    }
    // Number literal (including a leading minus).
    if (lx.i < lx.s.size() &&
        (std::isdigit(static_cast<unsigned char>(lx.s[lx.i])) ||
         (lx.s[lx.i] == '-' && lx.i + 1 < lx.s.size() &&
          std::isdigit(static_cast<unsigned char>(lx.s[lx.i + 1]))))) {
        std::string num;
        if (lx.s[lx.i] == '-') num.push_back(lx.s[lx.i++]);
        while (lx.i < lx.s.size() &&
               (std::isdigit(static_cast<unsigned char>(lx.s[lx.i])) ||
                lx.s[lx.i] == '.')) {
            num.push_back(lx.s[lx.i++]);
        }
        v.is_number = true;
        v.n = std::strtod(num.c_str(), nullptr);
        return v;
    }
    // Identifier / function call.
    if (lx.i < lx.s.size() &&
        (std::isalpha(static_cast<unsigned char>(lx.s[lx.i])) ||
         lx.s[lx.i] == '_')) {
        std::string name;
        while (lx.i < lx.s.size() &&
               (std::isalnum(static_cast<unsigned char>(lx.s[lx.i])) ||
                lx.s[lx.i] == '_')) {
            name.push_back(lx.s[lx.i++]);
        }
        std::string upper = upper_utf8(name);
        if (lx.match_char('(')) {
            // Only RECNO() / DELETED() are common in FOR clauses.
            std::vector<Value> args;
            lx.skip_ws();
            if (!lx.match_char(')')) {
                args.push_back(parse_atom(lx, t));
                while (lx.match_char(',')) args.push_back(parse_atom(lx, t));
                lx.match_char(')');
            }
            if (upper == "RECNO") {
                v.is_number = true;
                v.n = static_cast<double>(t.recno());
                return v;
            }
            if (upper == "DELETED") {
                v.is_number = true;
                v.n = t.is_deleted() ? 1.0 : 0.0;
                return v;
            }
            // Fallback: treat unknown call as empty string.
            v.is_number = false;
            return v;
        }
        // Field lookup.
        std::int32_t fidx = t.field_index(name);
        if (fidx < 0) {
            v.is_number = false;
            return v;
        }
        auto r = t.read_field(static_cast<std::uint16_t>(fidx));
        if (!r) { v.is_number = false; return v; }
        const auto& f = t.field_descriptor(static_cast<std::uint16_t>(fidx));
        if (f.type == drivers::DbfFieldType::Numeric ||
            f.type == drivers::DbfFieldType::Float    ||
            f.type == drivers::DbfFieldType::Integer  ||
            f.type == drivers::DbfFieldType::Double   ||
            f.type == drivers::DbfFieldType::Currency ||
            f.type == drivers::DbfFieldType::AdtMoney) {
            v.is_number = true;
            v.n = r.value().as_double;
        } else {
            v.is_number = false;
            std::string sv = r.value().as_string;
            while (!sv.empty() && sv.back() == ' ') sv.pop_back();
            v.s = sv;
        }
        return v;
    }
    return v;
}

bool eval_cmp(Lex& lx, Table& t) {
    lx.skip_ws();
    if (lx.match_char('(')) {
        bool inner = eval_or(lx, t);
        lx.match_char(')');
        return inner;
    }
    Value lhs = parse_atom(lx, t);
    lx.skip_ws();
    // Pick a comparison operator (longest match first).
    std::string op;
    auto try_op = [&](const char* s) {
        std::size_t L = std::strlen(s);
        if (lx.i + L > lx.s.size()) return false;
        if (std::memcmp(lx.s.data() + lx.i, s, L) != 0) return false;
        op.assign(s);
        lx.i += L;
        return true;
    };
    if (!try_op("==") && !try_op("!=") && !try_op("<>") &&
        !try_op(">=") && !try_op("<=") &&
        !try_op("=")  && !try_op(">")  && !try_op("<")  &&
        !try_op("#")) {
        // No comparison operator → treat lhs as truthy.
        return lhs.is_number ? (lhs.n != 0.0) : !lhs.s.empty();
    }
    Value rhs = parse_atom(lx, t);
    int cmp;
    if (lhs.is_number || rhs.is_number) {
        double a = lhs.is_number ? lhs.n : std::strtod(lhs.s.c_str(), nullptr);
        double b = rhs.is_number ? rhs.n : std::strtod(rhs.s.c_str(), nullptr);
        cmp = (a < b) ? -1 : (a > b ? 1 : 0);
    } else {
        cmp = lhs.s.compare(rhs.s);
        if (cmp < 0) cmp = -1; else if (cmp > 0) cmp = 1;
    }
    if (op == "==" || op == "=") return cmp == 0;
    if (op == "!=" || op == "<>" || op == "#") return cmp != 0;
    if (op == ">")  return cmp >  0;
    if (op == ">=") return cmp >= 0;
    if (op == "<")  return cmp <  0;
    if (op == "<=") return cmp <= 0;
    return true;
}

bool eval_not(Lex& lx, Table& t) {
    lx.skip_ws();
    if (lx.match_kw(".NOT.")) {
        return !eval_not(lx, t);
    }
    if (lx.match_char('!')) {
        return !eval_not(lx, t);
    }
    return eval_cmp(lx, t);
}

bool eval_and(Lex& lx, Table& t) {
    bool a = eval_not(lx, t);
    while (lx.match_kw(".AND.")) {
        bool b = eval_not(lx, t);
        a = a && b;
    }
    return a;
}

bool eval_or(Lex& lx, Table& t) {
    bool a = eval_and(lx, t);
    while (lx.match_kw(".OR.")) {
        bool b = eval_and(lx, t);
        a = a || b;
    }
    return a;
}

} // anonymous namespace

bool evaluate_index_expr_truthy(Table& t, const std::string& expr) {
    const std::string e = strip_alias_qualifiers(expr);
    if (e.empty()) return true;
    Lex lx(e);
    return eval_or(lx, t);
}

} // namespace openads::engine
