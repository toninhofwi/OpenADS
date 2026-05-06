#pragma once

#include <cstdint>

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
    AE_REMOTE_ERROR             = 5172
};

} // namespace openads
