#include "sql_backend/uri.h"

namespace openads::sql_backend {

namespace {

std::string url_decode_key(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(raw[i + 1]);
            const int lo = hex(raw[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (raw[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(raw[i]);
    }
    return out;
}

} // namespace

bool parse_sqlite_uri(const std::string& uri, SqliteUri& out) {
    static constexpr const char* kPrefix = "sqlite://";
    const auto plen = std::char_traits<char>::length(kPrefix);
    if (uri.size() < plen) return false;
    if (uri.compare(0, plen, kPrefix) != 0) return false;

    std::string rest = uri.substr(plen);
    if (rest.empty()) return false;

    const auto qpos = rest.find('?');
    if (qpos == std::string::npos) {
        out.path       = rest;
        out.cipher_key = {};
        return true;
    }

    out.path = rest.substr(0, qpos);
    if (out.path.empty()) return false;

    std::string query = rest.substr(qpos + 1);
    out.cipher_key    = {};
    while (!query.empty()) {
        const auto amp = query.find('&');
        const std::string pair =
            amp == std::string::npos ? query : query.substr(0, amp);
        query = amp == std::string::npos ? std::string{}
                                         : query.substr(amp + 1);
        const auto eq = pair.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = pair.substr(0, eq);
        const std::string val  = pair.substr(eq + 1);
        if (name == "key") {
            out.cipher_key = url_decode_key(val);
        }
    }
    return true;
}

} // namespace openads::sql_backend