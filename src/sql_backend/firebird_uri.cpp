#include "sql_backend/firebird_uri.h"

#include <cctype>
#include <string>

namespace openads::sql_backend {

namespace {

// Strip the scheme prefix; returns true and sets `rest` on a match.
bool strip_scheme(const std::string& uri, std::string& rest) {
    static constexpr const char* kLong  = "firebird://";
    static constexpr const char* kShort = "fb://";
    const auto llen = std::char_traits<char>::length(kLong);
    const auto slen = std::char_traits<char>::length(kShort);
    if (uri.size() >= llen && uri.compare(0, llen, kLong) == 0) {
        rest = uri.substr(llen);
        return true;
    }
    if (uri.size() >= slen && uri.compare(0, slen, kShort) == 0) {
        rest = uri.substr(slen);
        return true;
    }
    return false;
}

// Parse `&`-separated `key=value` pairs into the URI's option fields.
void apply_query(const std::string& query, FirebirdUri& out) {
    std::size_t pos = 0;
    while (pos < query.size()) {
        auto amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        const std::string pair = query.substr(pos, amp - pos);
        const auto eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = pair.substr(0, eq);
            std::string val = pair.substr(eq + 1);
            for (char& c : key) c = static_cast<char>(std::tolower(
                                       static_cast<unsigned char>(c)));
            if (key == "charset")       out.charset  = val;
            else if (key == "role")     out.role     = val;
            else if (key == "user")     { if (out.user.empty())     out.user = val; }
            else if (key == "password") { if (out.password.empty()) out.password = val; }
        }
        pos = amp + 1;
    }
}

} // namespace

std::string FirebirdUri::attach_string() const {
    if (embedded) return dbpath;
    std::string s = host;
    if (port > 0) {
        s += '/';
        s += std::to_string(port);
    }
    s += ':';
    s += dbpath;
    return s;
}

bool parse_firebird_uri(const std::string& uri, FirebirdUri& out) {
    out = FirebirdUri{};

    std::string rest;
    if (!strip_scheme(uri, rest)) return false;

    // Split off the option query string (?key=value&...).
    const auto qpos = rest.find('?');
    std::string query;
    if (qpos != std::string::npos) {
        query = rest.substr(qpos + 1);
        rest  = rest.substr(0, qpos);
    }

    // Optional inline credentials: user[:password]@  (only when the '@'
    // precedes the first path separator, so a password containing '/' is
    // not required but an '@' inside the path can't be mistaken for creds).
    const auto slash = rest.find('/');
    const auto at    = rest.find('@');
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
        const std::string cred = rest.substr(0, at);
        rest = rest.substr(at + 1);
        const auto colon = cred.find(':');
        if (colon == std::string::npos) {
            out.user = cred;
        } else {
            out.user     = cred.substr(0, colon);
            out.password = cred.substr(colon + 1);
        }
    }

    if (!rest.empty() && rest.front() == '/') {
        // Empty authority => embedded. The remainder (after the leading
        // slash) is the bare database path.
        out.embedded = true;
        out.dbpath   = rest.substr(1);
    } else {
        // Server form: authority is everything up to the first '/'.
        const auto sep = rest.find('/');
        if (sep == std::string::npos) return false; // no database path
        std::string authority = rest.substr(0, sep);
        out.dbpath            = rest.substr(sep + 1);
        if (authority.empty()) return false;

        const auto colon = authority.rfind(':');
        if (colon == std::string::npos) {
            out.host = authority;
        } else {
            out.host = authority.substr(0, colon);
            const std::string portstr = authority.substr(colon + 1);
            if (portstr.empty()) return false;
            int p = 0;
            for (char c : portstr) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
                p = p * 10 + (c - '0');
            }
            out.port = p;
        }
        if (out.host.empty()) return false;
    }

    apply_query(query, out);

    return !out.dbpath.empty();
}

} // namespace openads::sql_backend
