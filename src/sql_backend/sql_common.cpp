#include "sql_backend/sql_common.h"

#include <algorithm>
#include <cctype>

namespace openads::sql_backend {

namespace {

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(),
            s.end());
    return s;
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

util::Result<ParsedIndexExpr> parse_index_expr(const std::string& expr) {
    const std::string trimmed = trim_copy(expr);
    if (trimmed.empty()) {
        return util::Error{5001, 0, "empty index expression", ""};
    }

    ParsedIndexExpr out;
    if (trimmed.size() > 7 &&
        iequals(trimmed.substr(0, 6), "upper(") &&
        trimmed.back() == ')') {
        out.kind   = IndexExprKind::UpperColumn;
        out.column = trim_copy(trimmed.substr(6, trimmed.size() - 7));
    } else {
        out.kind   = IndexExprKind::Column;
        out.column = trimmed;
    }

    if (!is_safe_identifier(out.column)) {
        return util::Error{5001, 0, "unsupported index expression", expr};
    }
    return out;
}

bool is_safe_identifier(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_';
        if (!ok) return false;
    }
    return true;
}

} // namespace openads::sql_backend
