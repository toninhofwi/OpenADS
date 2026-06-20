#pragma once

#include <string>

namespace openads::sql_backend {

// Full libpq connection URI after `postgresql://` / `postgres://` prefix.
struct PostgresUri {
    std::string conninfo;
};

bool parse_postgres_uri(const std::string& uri, PostgresUri& out);

} // namespace openads::sql_backend