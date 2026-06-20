#include "sql_backend/postgres_backend.h"

#include "openads/ace.h"

#include <cctype>
#include <cstdio>

#if defined(OPENADS_WITH_POSTGRESQL)
#include <libpq-fe.h>
#endif

namespace openads::sql_backend {

PostgresTable::FieldDesc map_pg_column(const char* name,
                                       const char* data_type,
                                       bool nullable,
                                       int char_max_len,
                                       int numeric_precision,
                                       int numeric_scale) {
    PostgresTable::FieldDesc fd;
    fd.name     = name ? name : "";
    fd.nullable = nullable;

    std::string dt = data_type ? data_type : "";
    for (auto& c : dt) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (dt.find("int") != std::string::npos ||
        dt == "smallint" || dt == "bigint" || dt == "serial" ||
        dt == "bigserial") {
        fd.type     = ADS_INTEGER;
        fd.length   = 4;
        fd.decimals = 0;
    } else if (dt.find("double") != std::string::npos ||
               dt.find("real") != std::string::npos ||
               dt.find("numeric") != std::string::npos ||
               dt.find("decimal") != std::string::npos ||
               dt.find("money") != std::string::npos) {
        fd.type     = ADS_DOUBLE;
        fd.length   = 8;
        fd.decimals = numeric_scale > 0
                          ? static_cast<std::uint16_t>(numeric_scale)
                          : 6;
        (void)numeric_precision;
    } else if (dt.find("bytea") != std::string::npos) {
        fd.type     = ADS_BINARY;
        fd.length   = 10;
        fd.decimals = 0;
    } else {
        fd.type     = ADS_STRING;
        fd.length   = char_max_len > 0
                          ? static_cast<std::uint32_t>(char_max_len)
                          : 64;
        fd.decimals = 0;
    }
    return fd;
}

#if defined(OPENADS_WITH_POSTGRESQL)

std::string format_pg_value(pg_result* res, int row, int col, bool& is_null) {
    is_null = false;
    if (PQgetisnull(res, row, col)) {
        is_null = true;
        return {};
    }
    const char* val = PQgetvalue(res, row, col);
    if (val == nullptr) {
        is_null = true;
        return {};
    }
    return val;
}

util::Error postgres_error(const char* context, const char* msg) {
    return util::Error{5001, 0,
                       std::string(context) + ": " + (msg ? msg : ""),
                       ""};
}

#else

std::string format_pg_value(pg_result*, int, int, bool& is_null) {
    is_null = true;
    return {};
}

util::Error postgres_error(const char* context, const char* msg) {
    (void)context;
    (void)msg;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
}

#endif

std::size_t field_index_ci(const PostgresTable& tbl, const std::string& name) {
    std::string want = name;
    for (auto& c : want) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < tbl.fields.size(); ++i) {
        const std::string& have = tbl.fields[i].name;
        if (have.size() != want.size()) continue;
        bool eq = true;
        for (std::size_t k = 0; k < have.size(); ++k) {
            if (static_cast<char>(
                    std::toupper(static_cast<unsigned char>(have[k]))) !=
                want[k]) {
                eq = false;
                break;
            }
        }
        if (eq) return i;
    }
    return static_cast<std::size_t>(-1);
}

} // namespace openads::sql_backend