#include "sql_backend/odbc_backend.h"

#include "openads/ace.h"

#include <cctype>

namespace openads::sql_backend {

namespace {

// ODBC SQL type codes (sql.h / sqlext.h). Kept as local constants so
// this translation unit does not depend on the ODBC headers.
constexpr int kSqlChar          = 1;
constexpr int kSqlNumeric       = 2;
constexpr int kSqlDecimal       = 3;
constexpr int kSqlInteger       = 4;
constexpr int kSqlSmallint      = 5;
constexpr int kSqlFloat         = 6;
constexpr int kSqlReal          = 7;
constexpr int kSqlDouble        = 8;
constexpr int kSqlVarchar       = 12;
constexpr int kSqlLongVarchar   = -1;
constexpr int kSqlBinary        = -2;
constexpr int kSqlVarbinary     = -3;
constexpr int kSqlLongVarbinary = -4;
constexpr int kSqlBigint        = -5;
constexpr int kSqlTinyint       = -6;
constexpr int kSqlBit           = -7;
constexpr int kSqlDate          = 9;
constexpr int kSqlTime          = 10;
constexpr int kSqlTimestamp     = 11;
constexpr int kSqlTypeDate      = 91;
constexpr int kSqlTypeTime      = 92;
constexpr int kSqlTypeTimestamp = 93;

} // namespace

OdbcTable::FieldDesc map_odbc_column(const std::string& name,
                                     int sql_type,
                                     bool nullable,
                                     int column_size,
                                     int decimal_digits) {
    OdbcTable::FieldDesc fd;
    fd.name        = name;
    fd.nullable    = nullable;
    fd.sql_type    = sql_type;
    fd.column_size = column_size > 0
                         ? static_cast<std::uint32_t>(column_size) : 0;

    switch (sql_type) {
        case kSqlInteger:
        case kSqlSmallint:
        case kSqlTinyint:
        case kSqlBigint:
            fd.type     = ADS_INTEGER;
            fd.length   = 4;
            fd.decimals = 0;
            break;
        case kSqlNumeric:
        case kSqlDecimal:
        case kSqlFloat:
        case kSqlReal:
        case kSqlDouble:
            fd.type     = ADS_DOUBLE;
            fd.length   = 8;
            fd.decimals = decimal_digits > 0
                              ? static_cast<std::uint16_t>(decimal_digits)
                              : 6;
            break;
        case kSqlBit:
            fd.type     = ADS_LOGICAL;
            fd.length   = 1;
            fd.decimals = 0;
            break;
        case kSqlBinary:
        case kSqlVarbinary:
        case kSqlLongVarbinary:
            fd.type     = ADS_BINARY;
            fd.length   = 10;
            fd.decimals = 0;
            break;
        case kSqlDate:
        case kSqlTime:
        case kSqlTimestamp:
        case kSqlTypeDate:
        case kSqlTypeTime:
        case kSqlTypeTimestamp:
            fd.type     = ADS_DATE;
            fd.length   = 8;
            fd.decimals = 0;
            break;
        case kSqlChar:
        case kSqlVarchar:
        case kSqlLongVarchar:
        default:
            fd.type     = ADS_STRING;
            fd.length   = column_size > 0
                              ? static_cast<std::uint32_t>(column_size)
                              : 64;
            fd.decimals = 0;
            break;
    }
    return fd;
}

util::Error odbc_error(const char* context, const std::string& detail) {
    std::string msg = context ? context : "";
    if (!detail.empty()) {
        msg += ": ";
        msg += detail;
    }
    return util::Error{5001, 0, msg, ""};
}

std::size_t field_index_ci(const OdbcTable& tbl, const std::string& name) {
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
