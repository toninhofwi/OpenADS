#include "sql/parser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace openads::sql {

namespace {

class Cursor {
public:
    explicit Cursor(const std::string& s) : s_(s) {}

    void skip_ws() {
        while (pos_ < s_.size() &&
               std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }

    bool eof() const { return pos_ >= s_.size(); }

    bool peek_keyword(const char* kw) const {
        std::size_t len = 0;
        while (kw[len] != '\0') ++len;
        std::size_t p = pos_;
        while (p < s_.size() && std::isspace(
                static_cast<unsigned char>(s_[p]))) ++p;
        if (p + len > s_.size()) return false;
        for (std::size_t i = 0; i < len; ++i) {
            char a = static_cast<char>(std::tolower(
                static_cast<unsigned char>(s_[p + i])));
            char b = static_cast<char>(std::tolower(
                static_cast<unsigned char>(kw[i])));
            if (a != b) return false;
        }
        if (p + len < s_.size()) {
            char c = s_[p + len];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                return false;
            }
        }
        return true;
    }

    bool match_keyword(const char* kw) {
        if (!peek_keyword(kw)) return false;
        skip_ws();
        std::size_t len = 0;
        while (kw[len] != '\0') ++len;
        pos_ += len;
        return true;
    }

    bool match_char(char c) {
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    bool match_seq(const char* seq) {
        skip_ws();
        std::size_t len = 0;
        while (seq[len] != '\0') ++len;
        if (pos_ + len > s_.size()) return false;
        for (std::size_t i = 0; i < len; ++i) {
            if (s_[pos_ + i] != seq[i]) return false;
        }
        pos_ += len;
        return true;
    }

    std::string read_identifier_or_filename() {
        skip_ws();
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c)) ||
                c == '_' || c == '.' || c == '\\' || c == '/') {
                out.push_back(c);
                ++pos_;
            } else {
                break;
            }
        }
        return out;
    }

    std::string read_identifier() {
        skip_ws();
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                out.push_back(c);
                ++pos_;
            } else {
                break;
            }
        }
        return out;
    }

    util::Result<std::string> read_string_literal() {
        skip_ws();
        if (pos_ >= s_.size() || s_[pos_] != '\'') {
            return util::Error{7200, 0, "expected string literal", ""};
        }
        ++pos_;
        std::string out;
        while (pos_ < s_.size() && s_[pos_] != '\'') {
            out.push_back(s_[pos_]);
            ++pos_;
        }
        if (pos_ >= s_.size()) {
            return util::Error{7200, 0, "unterminated string literal", ""};
        }
        ++pos_;   // consume closing quote
        return out;
    }

    // Decimal numeric literal — optional sign, digits, optional `.` +
    // more digits. Returns the parsed double via stoul/stod. The
    // integer-only branch (no decimal point) stays compatible with
    // ASCII-numeric DBF columns.
    util::Result<double> read_numeric_literal() {
        skip_ws();
        std::size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
        bool any = false;
        while (pos_ < s_.size() &&
               std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_; any = true;
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            while (pos_ < s_.size() &&
                   std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                ++pos_; any = true;
            }
        }
        if (!any) {
            pos_ = start;
            return util::Error{7200, 0, "expected numeric literal", ""};
        }
        std::string tok(s_.substr(start, pos_ - start));
        return std::strtod(tok.c_str(), nullptr);
    }

    bool peek_char(char c) {
        skip_ws();
        return pos_ < s_.size() && s_[pos_] == c;
    }

private:
    const std::string& s_;
    std::size_t        pos_ = 0;
};

util::Result<std::unique_ptr<WhereExpr>>
parse_or_expr(Cursor& c, const std::string& sql);

util::Result<std::unique_ptr<WhereExpr>>
parse_cmp(Cursor& c, const std::string& sql) {
    auto node = std::make_unique<WhereExpr>();
    node->kind = WhereExpr::Kind::Cmp;

    if (c.match_keyword("CONTAINS")) {
        if (!c.match_char('(')) {
            return util::Error{7200, 0, "expected '(' after CONTAINS", sql};
        }
        node->cmp.column = c.read_identifier();
        if (node->cmp.column.empty()) {
            return util::Error{7200, 0,
                "expected column name in CONTAINS", sql};
        }
        if (!c.match_char(',')) {
            return util::Error{7200, 0,
                "expected ',' between CONTAINS column and query", sql};
        }
        auto lit = c.read_string_literal();
        if (!lit) return lit.error();
        node->cmp.literal = std::move(lit).value();
        if (!c.match_char(')')) {
            return util::Error{7200, 0, "expected ')' to close CONTAINS", sql};
        }
        node->cmp.op = WhereOp::Contains;
        return node;
    }

    node->cmp.column = c.read_identifier();
    if (node->cmp.column.empty()) {
        return util::Error{7200, 0,
            "expected column name in WHERE clause", sql};
    }
    c.skip_ws();
    if      (c.match_seq("<=")) node->cmp.op = WhereOp::Le;
    else if (c.match_seq(">=")) node->cmp.op = WhereOp::Ge;
    else if (c.match_seq("<>")) node->cmp.op = WhereOp::Ne;
    else if (c.match_seq("!=")) node->cmp.op = WhereOp::Ne;
    else if (c.match_char('=')) node->cmp.op = WhereOp::Eq;
    else if (c.match_char('<')) node->cmp.op = WhereOp::Lt;
    else if (c.match_char('>')) node->cmp.op = WhereOp::Gt;
    else {
        return util::Error{7200, 0,
            "expected =, !=, <>, <, >, <= or >= after column name", sql};
    }

    if (c.peek_char('\'')) {
        auto lit = c.read_string_literal();
        if (!lit) return lit.error();
        node->cmp.literal    = std::move(lit).value();
        node->cmp.is_numeric = false;
    } else {
        auto n = c.read_numeric_literal();
        if (!n) return n.error();
        node->cmp.is_numeric = true;
        node->cmp.number     = n.value();
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), "%.17g", n.value());
        node->cmp.literal    = tmp;
    }
    return node;
}

util::Result<std::unique_ptr<WhereExpr>>
parse_primary(Cursor& c, const std::string& sql) {
    if (c.match_keyword("NOT")) {
        auto inner = parse_primary(c, sql);
        if (!inner) return inner.error();
        auto n = std::make_unique<WhereExpr>();
        n->kind  = WhereExpr::Kind::Not;
        n->child = std::move(inner).value();
        return n;
    }
    if (c.match_char('(')) {
        auto inner = parse_or_expr(c, sql);
        if (!inner) return inner.error();
        if (!c.match_char(')')) {
            return util::Error{7200, 0,
                "expected ')' to close grouped WHERE expression", sql};
        }
        return inner;
    }
    return parse_cmp(c, sql);
}

util::Result<std::unique_ptr<WhereExpr>>
parse_and_expr(Cursor& c, const std::string& sql) {
    auto first = parse_primary(c, sql);
    if (!first) return first.error();
    std::unique_ptr<WhereExpr> lhs = std::move(first).value();
    while (c.match_keyword("AND")) {
        auto rhs = parse_primary(c, sql);
        if (!rhs) return rhs.error();
        auto node = std::make_unique<WhereExpr>();
        node->kind = WhereExpr::Kind::And;
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs).value());
        lhs = std::move(node);
    }
    return lhs;
}

util::Result<std::unique_ptr<WhereExpr>>
parse_or_expr(Cursor& c, const std::string& sql) {
    auto first = parse_and_expr(c, sql);
    if (!first) return first.error();
    std::unique_ptr<WhereExpr> lhs = std::move(first).value();
    while (c.match_keyword("OR")) {
        auto rhs = parse_and_expr(c, sql);
        if (!rhs) return rhs.error();
        auto node = std::make_unique<WhereExpr>();
        node->kind = WhereExpr::Kind::Or;
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs).value());
        lhs = std::move(node);
    }
    return lhs;
}

} // namespace

util::Result<SelectStmt> parse_select(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("SELECT")) {
        return util::Error{7200, 0, "expected SELECT", sql};
    }
    if (!c.match_char('*')) {
        return util::Error{7200, 0,
                           "M7.x only supports SELECT * (projection lists pending)",
                           sql};
    }
    if (!c.match_keyword("FROM")) {
        return util::Error{7200, 0, "expected FROM", sql};
    }
    SelectStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }

    // Optional WHERE — full boolean tree with AND / OR / NOT / parens
    // and either string or numeric literals.
    if (c.match_keyword("WHERE")) {
        auto root = parse_or_expr(c, sql);
        if (!root) return root.error();
        stmt.where = std::move(root).value();
    }

    // Optional ORDER BY — single column ascending or descending (M10.6).
    if (c.match_keyword("ORDER")) {
        if (!c.match_keyword("BY")) {
            return util::Error{7200, 0,
                "expected BY after ORDER", sql};
        }
        OrderBy ob;
        ob.column = c.read_identifier();
        if (ob.column.empty()) {
            return util::Error{7200, 0,
                "expected column name in ORDER BY", sql};
        }
        if      (c.match_keyword("DESC")) ob.descending = true;
        else if (c.match_keyword("ASC"))  ob.descending = false;
        stmt.order_by = std::move(ob);
    }

    c.match_char(';');
    return stmt;
}

bool sql_is_insert(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("INSERT");
}

bool sql_is_update(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("UPDATE");
}

bool sql_is_delete(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("DELETE");
}

util::Result<UpdateStmt> parse_update(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("UPDATE")) {
        return util::Error{7200, 0, "expected UPDATE", sql};
    }
    UpdateStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    if (!c.match_keyword("SET")) {
        return util::Error{7200, 0, "expected SET", sql};
    }
    for (;;) {
        UpdateAssign a;
        a.column = c.read_identifier();
        if (a.column.empty()) {
            return util::Error{7200, 0,
                "expected column name in SET clause", sql};
        }
        if (!c.match_char('=')) {
            return util::Error{7200, 0,
                "expected '=' after column name", sql};
        }
        if (c.peek_char('\'')) {
            auto s = c.read_string_literal();
            if (!s) return s.error();
            a.value.is_numeric = false;
            a.value.text       = std::move(s).value();
        } else {
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            a.value.is_numeric = true;
            a.value.number     = n.value();
        }
        stmt.assignments.push_back(std::move(a));
        if (c.match_char(',')) continue;
        break;
    }
    if (c.match_keyword("WHERE")) {
        auto root = parse_or_expr(c, sql);
        if (!root) return root.error();
        stmt.where = std::move(root).value();
    }
    c.match_char(';');
    return stmt;
}

util::Result<DeleteStmt> parse_delete(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("DELETE")) {
        return util::Error{7200, 0, "expected DELETE", sql};
    }
    if (!c.match_keyword("FROM")) {
        return util::Error{7200, 0, "expected FROM after DELETE", sql};
    }
    DeleteStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    if (c.match_keyword("WHERE")) {
        auto root = parse_or_expr(c, sql);
        if (!root) return root.error();
        stmt.where = std::move(root).value();
    }
    c.match_char(';');
    return stmt;
}

util::Result<InsertStmt> parse_insert(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("INSERT")) {
        return util::Error{7200, 0, "expected INSERT", sql};
    }
    if (!c.match_keyword("INTO")) {
        return util::Error{7200, 0, "expected INTO after INSERT", sql};
    }
    InsertStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }

    // Optional column list. Older xBase apps omit it (column order
    // matches table definition); we require it for clarity.
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' to open INSERT column list", sql};
    }
    for (;;) {
        std::string col = c.read_identifier();
        if (col.empty()) {
            return util::Error{7200, 0,
                "expected column name in INSERT list", sql};
        }
        stmt.columns.push_back(std::move(col));
        if (c.match_char(',')) continue;
        break;
    }
    if (!c.match_char(')')) {
        return util::Error{7200, 0,
            "expected ')' to close INSERT column list", sql};
    }

    if (!c.match_keyword("VALUES")) {
        return util::Error{7200, 0, "expected VALUES", sql};
    }
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' to open VALUES list", sql};
    }
    for (;;) {
        InsertLiteral lit;
        if (c.peek_char('\'')) {
            auto s = c.read_string_literal();
            if (!s) return s.error();
            lit.is_numeric = false;
            lit.text       = std::move(s).value();
        } else {
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            lit.is_numeric = true;
            lit.number     = n.value();
        }
        stmt.values.push_back(std::move(lit));
        if (c.match_char(',')) continue;
        break;
    }
    if (!c.match_char(')')) {
        return util::Error{7200, 0,
            "expected ')' to close VALUES list", sql};
    }

    if (stmt.columns.size() != stmt.values.size()) {
        return util::Error{7200, 0,
            "INSERT column count must match VALUES count", sql};
    }

    c.match_char(';');
    return stmt;
}

} // namespace openads::sql
