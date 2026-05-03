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

    bool match_keyword(const char* kw) {
        skip_ws();
        std::size_t len = 0;
        while (kw[len] != '\0') ++len;
        if (pos_ + len > s_.size()) return false;
        for (std::size_t i = 0; i < len; ++i) {
            char a = static_cast<char>(std::tolower(
                static_cast<unsigned char>(s_[pos_ + i])));
            char b = static_cast<char>(std::tolower(
                static_cast<unsigned char>(kw[i])));
            if (a != b) return false;
        }
        // Make sure the next char (if any) is not a word character.
        if (pos_ + len < s_.size()) {
            char c = s_[pos_ + len];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                return false;
            }
        }
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
                           "M7.1 only supports SELECT * (projection lists pending)",
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
    // Optional trailing semicolon.
    c.match_char(';');
    return stmt;
}

} // namespace openads::sql
