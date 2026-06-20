#include "sql_backend/sqlite_backend.h"

#if defined(OPENADS_WITH_SQLITE)

#include "openads/ace.h"

#include <cctype>
#include <cstdio>

namespace openads::sql_backend {

// is_safe_identifier lives in sql_common.cpp (shared by every SQL backend) so
// that an integration build with SQLite + another SQL backend does not get a
// duplicate-symbol clash. Declared in sqlite_backend.h / sql_common.h.

SqliteTable::FieldDesc map_sqlite_column(const char* name,
                                         const char* declared_type,
                                         bool notnull) {
    SqliteTable::FieldDesc fd;
    fd.name     = name ? name : "";
    fd.nullable = !notnull;

    std::string decl = declared_type ? declared_type : "";
    for (auto& c : decl) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    if (decl.find("INT") != std::string::npos) {
        fd.type     = ADS_INTEGER;
        fd.length   = 4;
        fd.decimals = 0;
    } else if (decl.find("REAL") != std::string::npos ||
               decl.find("FLOA") != std::string::npos ||
               decl.find("DOUB") != std::string::npos) {
        fd.type     = ADS_DOUBLE;
        fd.length   = 8;
        fd.decimals = 6;
    } else if (decl.find("BLOB") != std::string::npos) {
        fd.type     = ADS_BINARY;
        fd.length   = 10;
        fd.decimals = 0;
    } else {
        fd.type     = ADS_STRING;
        fd.length   = 64;
        fd.decimals = 0;
    }
    return fd;
}

std::string format_sqlite_value(sqlite3_stmt* stmt, int col, bool& is_null) {
    is_null = false;
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        is_null = true;
        return {};
    }
    switch (sqlite3_column_type(stmt, col)) {
        case SQLITE_INTEGER:
            return std::to_string(sqlite3_column_int64(stmt, col));
        case SQLITE_FLOAT: {
            // %.17g round-trips an IEEE-754 double without precision loss;
            // std::to_string clamps to 6 fractional digits.
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g",
                          sqlite3_column_double(stmt, col));
            return buf;
        }
        case SQLITE_BLOB: {
            const void* blob = sqlite3_column_blob(stmt, col);
            int         n    = sqlite3_column_bytes(stmt, col);
            if (blob == nullptr || n <= 0) return {};
            return std::string(static_cast<const char*>(blob),
                               static_cast<std::size_t>(n));
        }
        case SQLITE_TEXT:
        default: {
            const unsigned char* txt = sqlite3_column_text(stmt, col);
            if (txt == nullptr) {
                is_null = true;
                return {};
            }
            return reinterpret_cast<const char*>(txt);
        }
    }
}

util::Error sqlite_error(sqlite3* db, const char* context) {
    const char* msg = db ? sqlite3_errmsg(db) : "sqlite error";
    return util::Error{5001, 0,
                       std::string(context) + ": " + (msg ? msg : ""),
                       ""};
}

#if defined(OPENADS_WITH_SQLCIPHER)
namespace {

[[maybe_unused]] util::Result<void> exec_pragma(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "pragma failed";
        if (err) sqlite3_free(err);
        return util::Error{5001, 0, msg, ""};
    }
    return util::Result<void>{};
}

[[maybe_unused]] std::string escape_pragma_key(const std::string& key) {
    std::string out;
    out.reserve(key.size());
    for (char c : key) {
        if (c == '\'') out.append("''");
        else out.push_back(c);
    }
    return out;
}

} // namespace
#endif  // OPENADS_WITH_SQLCIPHER

util::Result<void> apply_cipher_key(sqlite3* db, const std::string& key) {
    if (key.empty()) return util::Result<void>{};
#if defined(OPENADS_WITH_SQLCIPHER)
    // Select the SQLCipher-compatible cipher scheme BEFORE keying; `legacy=4`
    // pins the SQLCipher v4 page format (kdf_iter 256000, HMAC-SHA512, 4096B
    // pages). `PRAGMA key` then derives the key and unlocks the database.
    const std::string esc = escape_pragma_key(key);
    if (auto r = exec_pragma(db, "PRAGMA cipher='sqlcipher'"); !r) return r;
    if (auto r = exec_pragma(db, "PRAGMA legacy=4"); !r) return r;
    if (auto r = exec_pragma(db, "PRAGMA key='" + esc + "'"); !r) return r;
    return util::Result<void>{};
#else
    (void)db;
    return util::Error{
        5004, 0,
        "encrypted sqlite:// requires OPENADS_WITH_SQLCIPHER=ON at build time",
        ""};
#endif
}

std::size_t field_index_ci(const SqliteTable& tbl, const std::string& name) {
    std::string want = name;
    for (auto& c : want) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < tbl.fields.size(); ++i) {
        std::string have = tbl.fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return static_cast<std::size_t>(-1);
}

} // namespace openads::sql_backend

#endif // OPENADS_WITH_SQLITE