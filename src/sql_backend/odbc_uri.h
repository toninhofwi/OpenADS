#pragma once

#include <string>

namespace openads::sql_backend {

// An `odbc://` (or `odbc:`) connection URI. The remainder after the
// scheme is handed verbatim to SQLDriverConnect as the ODBC connection
// string, so any DSN-less driver string or `DSN=name;…` form works:
//   odbc://Driver={Some Driver};Server=host;Database=db;UID=u;PWD=p;
//   odbc:DSN=mydsn;UID=u;PWD=p;
struct OdbcUri {
    std::string connstr;
    // Optional password kept out of |connstr| (Oracle URIs, hardened ODBC).
    std::string password;
};

bool parse_odbc_uri(const std::string& uri, OdbcUri& out);

} // namespace openads::sql_backend
