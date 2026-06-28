#pragma once

#include <string>

namespace openads::sql_backend {

// `oracle://user:pass@host[:port]/service` — SQLRDD-style alias that routes
// through the ODBC backend using a standard Oracle ODBC connection string.
// Requires OPENADS_WITH_ODBC=ON and an installed Oracle ODBC driver.
struct OracleUri {
    std::string user;
    std::string password;
    std::string host;
    int         port     = 1521;
    std::string service;  // DBQ / service name
};

bool parse_oracle_uri(const std::string& uri, OracleUri& out);

// Build an ODBC driver-manager connection string for Oracle ODBC.
std::string oracle_to_odbc_connstr(const OracleUri& uri);

}  // namespace openads::sql_backend