#include "sql_backend/odbc_uri.h"

namespace openads::sql_backend {

bool parse_odbc_uri(const std::string& uri, OdbcUri& out) {
    static constexpr const char* kSlash = "odbc://";
    static constexpr const char* kBare  = "odbc:";
    const auto slen = std::char_traits<char>::length(kSlash);
    const auto blen = std::char_traits<char>::length(kBare);

    if (uri.size() >= slen && uri.compare(0, slen, kSlash) == 0) {
        out = OdbcUri{};
        out.connstr = uri.substr(slen);
    } else if (uri.size() >= blen && uri.compare(0, blen, kBare) == 0) {
        out = OdbcUri{};
        out.connstr = uri.substr(blen);
    } else {
        return false;
    }
    return !out.connstr.empty();
}

} // namespace openads::sql_backend
