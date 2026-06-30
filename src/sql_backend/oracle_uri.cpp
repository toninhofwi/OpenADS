#include "sql_backend/oracle_uri.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace openads::sql_backend {

namespace {

std::string percent_decode(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()
                && std::isxdigit(static_cast<unsigned char>(src[i + 1]))
                && std::isxdigit(static_cast<unsigned char>(src[i + 2]))) {
            const char hex[3] = {src[i + 1], src[i + 2], '\0'};
            out += static_cast<char>(static_cast<unsigned char>(
                std::strtoul(hex, nullptr, 16)));
            i += 2;
        } else {
            out += src[i];
        }
    }
    return out;
}

}  // namespace

bool parse_oracle_uri(const std::string& uri, OracleUri& out) {
    static constexpr const char* kScheme = "oracle://";
    static const std::size_t kLen = 9;
    if (uri.size() < kLen || uri.compare(0, kLen, kScheme) != 0) {
        return false;
    }
    const std::string rest = uri.substr(kLen);
    const auto at_pos = rest.find('@');
    if (at_pos == std::string::npos) return false;

    const std::string credentials = rest.substr(0, at_pos);
    const std::string hostpart    = rest.substr(at_pos + 1);

    const auto colon_pos = credentials.find(':');
    if (colon_pos == std::string::npos) {
        out.user     = percent_decode(credentials);
        out.password = {};
    } else {
        out.user     = percent_decode(credentials.substr(0, colon_pos));
        out.password = percent_decode(credentials.substr(colon_pos + 1));
    }

    const auto slash_pos = hostpart.find('/');
    if (slash_pos == std::string::npos) return false;

    const std::string hostport = hostpart.substr(0, slash_pos);
    out.service                = hostpart.substr(slash_pos + 1);
    if (out.service.empty()) return false;

    const auto port_sep = hostport.rfind(':');
    if (port_sep == std::string::npos) {
        out.host = hostport;
        out.port = 1521;
    } else {
        out.host = hostport.substr(0, port_sep);
        out.port = std::atoi(hostport.substr(port_sep + 1).c_str());
        if (out.port <= 0) out.port = 1521;
    }
    if (out.host.empty()) return false;
    return true;
}

std::string oracle_to_odbc_connstr(const OracleUri& uri) {
    std::ostringstream os;
    // Password is supplied separately at connect time (OdbcUri::password)
    // so it is not embedded in a persisted/logged connection string.
    os << "DRIVER={Oracle ODBC Driver};DBQ=//" << uri.host << ':' << uri.port
       << '/' << uri.service << ";UID=" << uri.user;
    return os.str();
}

}  // namespace openads::sql_backend