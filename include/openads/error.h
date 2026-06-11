#pragma once

#include <cstdint>

// openads/ace.h defines a subset of these names as preprocessor
// macros for clipper / Harbour-style consumers. Undefining them
// here keeps the enum members below from getting macro-expanded
// when both headers are pulled into the same translation unit.
#undef AE_SUCCESS
#undef AE_INTERNAL_ERROR
#undef AE_FUNCTION_NOT_AVAILABLE
#undef AE_LOCKED
#undef AE_LOCK_FAILED
#undef AE_NO_CONNECTION
#undef AE_NO_FILE_FOUND
#undef AE_COLUMN_NOT_FOUND
#undef AE_TABLE_NOT_FOUND
#undef AE_TABLE_CORRUPTED
#undef AE_INVALID_CONNECTION_HANDLE
#undef AE_PARSE_ERROR
#undef AE_INVALID_SQL_TOKEN
#undef AE_TYPE_MISMATCH
#undef AE_DIVISION_BY_ZERO
#undef AE_LOGIN_FAILED
#undef AE_REMOTE_ERROR
#undef AE_SAP_PERMS_NEED_IMPORT

namespace openads {

// Mirror of the ACE error code surface OpenADS will emit.
// See README "Error handling" section for the full table.
enum : std::uint32_t {
    AE_SUCCESS                  = 0,
    AE_INTERNAL_ERROR           = 5000,
    AE_FUNCTION_NOT_AVAILABLE   = 5004,
    AE_LOCKED                   = 5012,
    AE_LOCK_FAILED              = 5013,
    AE_NO_CONNECTION            = 5036,
    AE_NO_FILE_FOUND            = 5018,
    AE_COLUMN_NOT_FOUND         = 5063,
    AE_TABLE_NOT_FOUND          = 5066,
    AE_TABLE_CORRUPTED          = 5103,
    AE_INVALID_CONNECTION_HANDLE = 4097,
    AE_PARSE_ERROR              = 7200,
    AE_INVALID_SQL_TOKEN        = 7201,
    AE_TYPE_MISMATCH            = 7041,
    AE_DIVISION_BY_ZERO         = 7042,
    AE_LOGIN_FAILED             = 7077,
    AE_ACCESS_DENIED            = 7079,
    AE_REMOTE_ERROR             = 5172,
    AE_SAP_PERMS_NEED_IMPORT    = 5174, // DD has SAP-format permissions; run import tool
    AE_RI_VIOLATION             = 508   // referential integrity constraint failed
};

} // namespace openads
