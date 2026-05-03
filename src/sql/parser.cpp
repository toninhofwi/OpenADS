#include "sql/parser.h"

#include <cctype>
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

private:
    const std::string& s_;
    std::size_t        pos_ = 0;
};

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

    // Optional single-equality WHERE.
    if (c.match_keyword("WHERE")) {
        WhereEq w;
        w.column = c.read_identifier();
        if (w.column.empty()) {
            return util::Error{7200, 0, "expected column name after WHERE", sql};
        }
        if (!c.match_char('=')) {
            return util::Error{7200, 0,
                "M7.x WHERE supports only `column = '<literal>'`", sql};
        }
        auto lit = c.read_string_literal();
        if (!lit) return lit.error();
        w.literal = std::move(lit).value();
        stmt.where = std::move(w);
    }

    // Optional trailing semicolon.
    c.match_char(';');
    return stmt;
}

} // namespace openads::sql
