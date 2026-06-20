#include "sql_backend/postgres_uri.h"

namespace openads::sql_backend {

bool parse_postgres_uri(const std::string& uri, PostgresUri& out) {
    static constexpr const char* kPg   = "postgresql://";
    static constexpr const char* kPg2  = "postgres://";
    static constexpr const char* kPg3  = "pgsql://";
    const auto plen  = std::char_traits<char>::length(kPg);
    const auto plen2 = std::char_traits<char>::length(kPg2);
    const auto plen3 = std::char_traits<char>::length(kPg3);

    if (uri.size() >= plen && uri.compare(0, plen, kPg) == 0) {
        out.conninfo = uri;
        return true;
    }
    if (uri.size() >= plen2 && uri.compare(0, plen2, kPg2) == 0) {
        out.conninfo = "postgresql://" + uri.substr(plen2);
        return true;
    }
    if (uri.size() >= plen3 && uri.compare(0, plen3, kPg3) == 0) {
        out.conninfo = "postgresql://" + uri.substr(plen3);
        return true;
    }
    return false;
}

} // namespace openads::sql_backend