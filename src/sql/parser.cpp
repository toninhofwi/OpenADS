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

    // Advance past one raw character (no whitespace skip) and return
    // it. Used by the CREATE INDEX expression scanner that wants to
    // capture the source text verbatim until the matching ')'.
    char consume_char() {
        if (pos_ >= s_.size()) return '\0';
        return s_[pos_++];
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

    // M10.15: `<col> IN (<lit>, <lit>, …)` or `<col> IN (<sub-SELECT>)`.
    if (c.match_keyword("IN")) {
        if (!c.match_char('(')) {
            return util::Error{7200, 0,
                "expected '(' after IN", sql};
        }
        node->kind = WhereExpr::Kind::In;
        node->in_clause.column = node->cmp.column;
        // (cmp's other fields stay default-zeroed; the In branch
        // ignores them.)
        if (c.match_keyword("SELECT")) {
            // Re-parse the nested SELECT — capture text up to the
            // matching ')'. consume_char (no whitespace skip) is the
            // only reader so the inner buffer keeps every space and
            // tab byte the inner parser will need.
            std::string inner = "SELECT ";
            int depth = 1;
            while (depth > 0) {
                if (c.eof()) {
                    return util::Error{7200, 0,
                        "unterminated IN subquery", sql};
                }
                char ch = c.consume_char();
                if (ch == '(') { ++depth; inner.push_back('('); continue; }
                if (ch == ')') {
                    --depth;
                    if (depth == 0) break;
                    inner.push_back(')');
                    continue;
                }
                inner.push_back(ch);
            }
            auto sub = parse_select(inner);
            if (!sub) return sub.error();
            node->in_clause.subquery =
                std::make_unique<SelectStmt>(std::move(sub).value());
        } else {
            // Literal list.
            for (;;) {
                auto lit = c.read_string_literal();
                if (!lit) return lit.error();
                node->in_clause.literals.push_back(std::move(lit).value());
                if (c.match_char(',')) continue;
                break;
            }
            if (!c.match_char(')')) {
                return util::Error{7200, 0,
                    "expected ')' to close IN list", sql};
            }
        }
        return node;
    }

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
    } else if (c.peek_char('(')) {
        // M10.18: scalar subquery — `(SELECT <col> FROM <t>)`. The
        // value is materialised at compile time and stored back in
        // node->cmp.literal / number.
        c.match_char('(');
        if (!c.match_keyword("SELECT")) {
            return util::Error{7200, 0,
                "expected SELECT inside scalar subquery", sql};
        }
        std::string inner = "SELECT ";
        int depth = 1;
        while (depth > 0) {
            if (c.eof()) {
                return util::Error{7200, 0,
                    "unterminated scalar subquery", sql};
            }
            char ch = c.consume_char();
            if (ch == '(') { ++depth; inner.push_back('('); continue; }
            if (ch == ')') {
                --depth;
                if (depth == 0) break;
                inner.push_back(')');
                continue;
            }
            inner.push_back(ch);
        }
        auto sub = parse_select(inner);
        if (!sub) return sub.error();
        node->cmp.subquery =
            std::make_unique<SelectStmt>(std::move(sub).value());
    } else {
        auto n = c.read_numeric_literal();
        if (n) {
            node->cmp.is_numeric = true;
            node->cmp.number     = n.value();
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.17g", n.value());
            node->cmp.literal    = tmp;
        } else {
            // M10.24: bare identifier on RHS — outer-column reference
            // for a correlated subquery (`b.x = a.y`). The executor
            // reads it from the outer cursor at evaluation time.
            std::string id = c.read_identifier();
            if (id.empty()) {
                return util::Error{7200, 0,
                    "expected string literal, number, or column "
                    "reference on RHS of comparison", sql};
            }
            node->cmp.is_outer_ref = true;
            node->cmp.outer_column = id;
            node->cmp.is_numeric   = false;
            node->cmp.literal      = id;       // diagnostic fallback
        }
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
    if (c.match_keyword("EXISTS")) {
        if (!c.match_char('(')) {
            return util::Error{7200, 0, "expected '(' after EXISTS", sql};
        }
        if (!c.match_keyword("SELECT")) {
            return util::Error{7200, 0,
                "expected SELECT inside EXISTS", sql};
        }
        std::string inner = "SELECT ";
        int depth = 1;
        while (depth > 0) {
            if (c.eof()) {
                return util::Error{7200, 0,
                    "unterminated EXISTS subquery", sql};
            }
            char ch = c.consume_char();
            if (ch == '(') { ++depth; inner.push_back('('); continue; }
            if (ch == ')') {
                --depth;
                if (depth == 0) break;
                inner.push_back(')');
                continue;
            }
            inner.push_back(ch);
        }
        auto sub = parse_select(inner);
        if (!sub) return sub.error();
        auto n = std::make_unique<WhereExpr>();
        n->kind = WhereExpr::Kind::Exists;
        n->exists_subquery =
            std::make_unique<SelectStmt>(std::move(sub).value());
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
    SelectStmt stmt;
    if (c.match_char('*')) {
        // SELECT * — projection stays empty (every column visible).
    } else {
        // Projection list (M10.8) or aggregate list (M10.10).
        // Aggregates take the form `KIND(*)` or `KIND(col)` where
        // KIND is one of COUNT / SUM / AVG / MIN / MAX. The first
        // item decides the mode for the whole list — mixing plain
        // columns and aggregates in the same SELECT is not
        // supported in this milestone.
        bool aggregate_mode = false;
        for (;;) {
            std::string head = c.read_identifier();
            if (head.empty()) {
                return util::Error{7200, 0,
                    "expected '*' or column name in SELECT", sql};
            }
            std::string upper;
            upper.reserve(head.size());
            for (char ch : head) {
                upper.push_back(static_cast<char>(std::toupper(
                    static_cast<unsigned char>(ch))));
            }
            bool is_agg_call = (upper == "COUNT" || upper == "SUM" ||
                                upper == "AVG"   || upper == "MIN" ||
                                upper == "MAX") && c.peek_char('(');

            if (is_agg_call) {
                if (!stmt.projection.empty()) {
                    return util::Error{7200, 0,
                        "mixing plain columns + aggregates in SELECT not supported", sql};
                }
                aggregate_mode = true;
                if (!c.match_char('(')) {
                    return util::Error{7200, 0,
                        "expected '(' after aggregate name", sql};
                }
                Aggregate agg;
                if (upper == "COUNT") agg.kind = AggregateKind::Count;
                if (upper == "SUM")   agg.kind = AggregateKind::Sum;
                if (upper == "AVG")   agg.kind = AggregateKind::Avg;
                if (upper == "MIN")   agg.kind = AggregateKind::Min;
                if (upper == "MAX")   agg.kind = AggregateKind::Max;
                if (c.match_char('*')) {
                    if (upper != "COUNT") {
                        return util::Error{7200, 0,
                            "* argument only valid for COUNT", sql};
                    }
                    agg.kind = AggregateKind::CountStar;
                } else {
                    std::string col = c.read_identifier();
                    if (col.empty()) {
                        return util::Error{7200, 0,
                            "expected column name inside aggregate", sql};
                    }
                    agg.column = std::move(col);
                }
                if (!c.match_char(')')) {
                    return util::Error{7200, 0,
                        "expected ')' to close aggregate", sql};
                }
                stmt.aggregates.push_back(std::move(agg));
            } else {
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "mixing plain columns + aggregates in SELECT not supported", sql};
                }
                stmt.projection.push_back(std::move(head));
            }
            if (c.match_char(',')) continue;
            break;
        }
    }
    if (!c.match_keyword("FROM")) {
        return util::Error{7200, 0, "expected FROM", sql};
    }
    stmt.table = c.read_identifier_or_filename();
    bool is_left_join  = false;
    bool is_right_join = false;
    bool is_full_join  = false;
    bool saw_join_keyword = false;
    if (c.match_keyword("LEFT")) {
        c.match_keyword("OUTER");   // optional
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after LEFT", sql};
        }
        is_left_join = true;
        saw_join_keyword = true;
    } else if (c.match_keyword("RIGHT")) {
        c.match_keyword("OUTER");   // optional
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after RIGHT", sql};
        }
        is_right_join = true;
        saw_join_keyword = true;
    } else if (c.match_keyword("FULL")) {
        c.match_keyword("OUTER");   // optional
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after FULL", sql};
        }
        is_full_join = true;
        saw_join_keyword = true;
    } else if (c.match_keyword("INNER")) {
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after INNER", sql};
        }
        saw_join_keyword = true;
    } else if (c.match_keyword("JOIN")) {
        // Bare JOIN — treated as INNER per SQL convention.
        saw_join_keyword = true;
    }
    if (saw_join_keyword) {
        JoinClause j;
        j.is_left  = is_left_join;
        j.is_right = is_right_join;
        j.is_full  = is_full_join;
        j.table = c.read_identifier_or_filename();
        if (j.table.empty()) {
            return util::Error{7200, 0,
                "expected table name after INNER JOIN", sql};
        }
        if (!c.match_keyword("ON")) {
            return util::Error{7200, 0,
                "expected ON for INNER JOIN", sql};
        }
        // Parse `<lcol> = <rcol>`. Both columns are bare identifiers
        // — qualified `tbl.col` syntax lands in a later milestone.
        j.left_column = c.read_identifier();
        if (j.left_column.empty()) {
            return util::Error{7200, 0,
                "expected left join column", sql};
        }
        if (!c.match_char('=')) {
            return util::Error{7200, 0,
                "expected '=' in JOIN ON clause", sql};
        }
        j.right_column = c.read_identifier();
        if (j.right_column.empty()) {
            return util::Error{7200, 0,
                "expected right join column", sql};
        }
        stmt.inner_join = std::move(j);
    }
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

    // M10.25 — GROUP BY <col>[, <col>...] [HAVING <agg> <op> <num>].
    // Sits between WHERE and ORDER BY in the SQL grammar.
    if (c.match_keyword("GROUP")) {
        if (!c.match_keyword("BY")) {
            return util::Error{7200, 0, "expected BY after GROUP", sql};
        }
        for (;;) {
            std::string col = c.read_identifier();
            if (col.empty()) {
                return util::Error{7200, 0,
                    "expected column name in GROUP BY", sql};
            }
            stmt.group_by.push_back(std::move(col));
            if (!c.match_char(',')) break;
        }
    }
    if (c.match_keyword("HAVING")) {
        std::string head = c.read_identifier();
        std::string upper;
        upper.reserve(head.size());
        for (char ch : head) {
            upper.push_back(static_cast<char>(std::toupper(
                static_cast<unsigned char>(ch))));
        }
        if (upper != "COUNT" && upper != "SUM" && upper != "AVG" &&
            upper != "MIN"   && upper != "MAX") {
            return util::Error{7200, 0,
                "HAVING expects an aggregate call (COUNT/SUM/AVG/MIN/MAX)", sql};
        }
        if (!c.match_char('(')) {
            return util::Error{7200, 0,
                "expected '(' after aggregate name in HAVING", sql};
        }
        HavingClause hc;
        if (upper == "COUNT") hc.agg.kind = AggregateKind::Count;
        if (upper == "SUM")   hc.agg.kind = AggregateKind::Sum;
        if (upper == "AVG")   hc.agg.kind = AggregateKind::Avg;
        if (upper == "MIN")   hc.agg.kind = AggregateKind::Min;
        if (upper == "MAX")   hc.agg.kind = AggregateKind::Max;
        if (c.match_char('*')) {
            if (upper != "COUNT") {
                return util::Error{7200, 0,
                    "* argument only valid for COUNT", sql};
            }
            hc.agg.kind = AggregateKind::CountStar;
        } else {
            std::string col = c.read_identifier();
            if (col.empty()) {
                return util::Error{7200, 0,
                    "expected column name inside HAVING aggregate", sql};
            }
            hc.agg.column = std::move(col);
        }
        if (!c.match_char(')')) {
            return util::Error{7200, 0,
                "expected ')' to close HAVING aggregate", sql};
        }
        if      (c.match_seq("<=")) hc.op = WhereOp::Le;
        else if (c.match_seq(">=")) hc.op = WhereOp::Ge;
        else if (c.match_seq("<>")) hc.op = WhereOp::Ne;
        else if (c.match_seq("!=")) hc.op = WhereOp::Ne;
        else if (c.match_char('=')) hc.op = WhereOp::Eq;
        else if (c.match_char('<')) hc.op = WhereOp::Lt;
        else if (c.match_char('>')) hc.op = WhereOp::Gt;
        else return util::Error{7200, 0,
            "expected =, !=, <>, <, >, <= or >= after HAVING aggregate", sql};
        auto n = c.read_numeric_literal();
        if (!n) return n.error();
        hc.num = n.value();
        stmt.having = std::move(hc);
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

bool sql_is_create_table(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("CREATE") && c.match_keyword("TABLE");
}

bool sql_is_create_index(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("CREATE") && c.match_keyword("INDEX");
}

util::Result<CreateTableStmt>
parse_create_table(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("CREATE")) {
        return util::Error{7200, 0, "expected CREATE", sql};
    }
    if (!c.match_keyword("TABLE")) {
        return util::Error{7200, 0, "expected TABLE", sql};
    }
    CreateTableStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' to open column list", sql};
    }
    for (;;) {
        CreateTableColumn col;
        col.name = c.read_identifier();
        if (col.name.empty()) {
            return util::Error{7200, 0,
                "expected column name in CREATE TABLE", sql};
        }
        col.type = c.read_identifier();
        if (col.type.empty()) {
            return util::Error{7200, 0,
                "expected type after column name", sql};
        }
        if (c.match_char('(')) {
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            col.length = static_cast<std::uint32_t>(n.value());
            if (c.match_char(',')) {
                auto d = c.read_numeric_literal();
                if (!d) return d.error();
                col.decimals = static_cast<std::uint32_t>(d.value());
            }
            if (!c.match_char(')')) {
                return util::Error{7200, 0,
                    "expected ')' to close column type", sql};
            }
        }
        stmt.columns.push_back(std::move(col));
        if (c.match_char(',')) continue;
        break;
    }
    if (!c.match_char(')')) {
        return util::Error{7200, 0,
            "expected ')' to close column list", sql};
    }
    c.match_char(';');
    return stmt;
}

util::Result<CreateIndexStmt>
parse_create_index(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("CREATE")) {
        return util::Error{7200, 0, "expected CREATE", sql};
    }
    if (!c.match_keyword("INDEX")) {
        return util::Error{7200, 0, "expected INDEX", sql};
    }
    CreateIndexStmt stmt;
    stmt.tag = c.read_identifier();
    if (stmt.tag.empty()) {
        return util::Error{7200, 0, "expected index tag name", sql};
    }
    if (!c.match_keyword("ON")) {
        return util::Error{7200, 0, "expected ON", sql};
    }
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' to open index expression", sql};
    }
    // Read everything up to the matching ')' as the expression. The
    // existing engine evaluator handles compound expressions like
    // UPPER(name) so identifiers + parens nest correctly. consume_char
    // skips no whitespace so the inner buffer keeps every byte.
    std::string expr;
    int depth = 1;
    while (depth > 0) {
        if (c.eof()) {
            return util::Error{7200, 0,
                "unterminated CREATE INDEX expression", sql};
        }
        char ch = c.consume_char();
        if (ch == '(') { ++depth; expr.push_back('('); continue; }
        if (ch == ')') {
            --depth;
            if (depth == 0) break;
            expr.push_back(')');
            continue;
        }
        expr.push_back(ch);
    }
    while (!expr.empty() &&
           std::isspace(static_cast<unsigned char>(expr.front()))) {
        expr.erase(expr.begin());
    }
    while (!expr.empty() &&
           std::isspace(static_cast<unsigned char>(expr.back()))) {
        expr.pop_back();
    }
    stmt.expression = std::move(expr);
    if (c.match_keyword("DESCENDING") || c.match_keyword("DESC")) {
        stmt.descending = true;
    }
    if (c.match_keyword("UNIQUE")) {
        stmt.unique = true;
    }
    c.match_char(';');
    return stmt;
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
