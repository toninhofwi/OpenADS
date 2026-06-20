#include "sql_backend/sqlite_backend.h"

#if defined(OPENADS_WITH_SQLITE)

#include "openads/ace.h"

#include <cctype>

namespace openads::sql_backend {

bool is_safe_identifier(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

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
        case SQLITE_FLOAT:
            return std::to_string(sqlite3_column_double(stmt, col));
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

namespace {

util::Result<void> exec_pragma(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "pragma failed";
        if (err) sqlite3_free(err);
        return util::Error{5001, 0, msg, ""};
    }
    return util::Result<void>{};
}

std::string escape_pragma_key(const std::string& key) {
    std::string out;
    out.reserve(key.size());
    for (char c : key) {
        if (c == '\'') out.append("''");
        else out.push_back(c);
    }
    return out;
}

} // namespace

util::Result<void> apply_cipher_key(sqlite3* db, const std::string& key) {
    if (key.empty()) return util::Result<void>{};
#if defined(OPENADS_WITH_SQLCIPHER)
    const std::string esc = escape_pragma_key(key);
    if (auto r = exec_pragma(db, "PRAGMA key='" + esc + "'"); !r) return r;
    if (auto r = exec_pragma(db, "PRAGMA cipher_page_size=4096"); !r) return r;
    if (auto r = exec_pragma(db, "PRAGMA kdf_iter=256000"); !r) return r;
    if (auto r = exec_pragma(db, "PRAGMA cipher_hmac_algorithm=HMAC_SHA512"); !r)
        return r;
    if (auto r = exec_pragma(db,
                             "PRAGMA cipher_kdf_algorithm=PBKDF2_HMAC_SHA512");
        !r)
        return r;
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