#pragma once

#include <string>

namespace openads::sql_backend {

struct SqliteUri {
    std::string path;
    // Optional passphrase when built with OPENADS_WITH_SQLCIPHER (?key=).
    std::string cipher_key;
};

// Parse `sqlite://<db_path>` with optional `?key=<passphrase>`.
// Returns false when the sqlite:// prefix is absent.
bool parse_sqlite_uri(const std::string& uri, SqliteUri& out);

} // namespace openads::sql_backend