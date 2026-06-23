#pragma once

#include <string>

namespace openads::sql_backend {

// A `firebird://` (or `fb://`) connection URI.
//
// Unlike the ODBC backend — which hands the remainder verbatim to a driver
// manager — the Firebird client API (`isc_attach_database`) needs a database
// string plus a structured parameter buffer (user / password / charset /
// role), so the URI is parsed into fields.
//
// Forms:
//   Embedded (no server, opens the file in-process — note the TRIPLE slash):
//     firebird:///C:/data/app.fdb
//     firebird:///var/lib/firebird/data/app.fdb
//   Server (TCP):
//     firebird://SYSDBA:masterkey@localhost/var/lib/firebird/data/app.fdb
//     firebird://SYSDBA:masterkey@localhost:3050/C:/data/app.fdb
//   Options (either form) as a query string:
//     ...app.fdb?charset=UTF8&role=MANAGER
//     ...app.fdb?user=SYSDBA&password=masterkey   (alternative to user:pass@)
//
// The triple-slash embedded form avoids the Windows drive-letter ambiguity
// (`C:` would otherwise look like host `C` port-less): an empty authority
// always means embedded.
struct FirebirdUri {
    bool        embedded = false;
    std::string host;          // empty when embedded
    int         port     = 0;  // 0 => driver default (3050)
    std::string dbpath;        // database file path or alias
    std::string user;
    std::string password;
    std::string role;
    std::string charset;

    // The database string handed to isc_attach_database. For the server form
    // this is the legacy `host[/port]:dbpath`; for embedded it is the bare
    // path (the embedded provider opens it directly).
    std::string attach_string() const;
};

bool parse_firebird_uri(const std::string& uri, FirebirdUri& out);

} // namespace openads::sql_backend
