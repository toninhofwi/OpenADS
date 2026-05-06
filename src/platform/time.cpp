#include "platform/time.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#  include <climits>
#endif

namespace openads::platform {

std::int64_t monotonic_nanos() {
    using clock = std::chrono::steady_clock;
    auto d = clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

std::int64_t utc_unix_micros() {
    using clock = std::chrono::system_clock;
    auto d = clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

std::string host_name() {
#if defined(_WIN32)
    char buf[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD n = static_cast<DWORD>(sizeof(buf));
    if (::GetComputerNameA(buf, &n)) {
        return std::string(buf, n);
    }
    return {};
#else
    // macOS doesn't define HOST_NAME_MAX in <limits.h>; fall back to
    // _POSIX_HOST_NAME_MAX (255) when absent. Linux glibc carries
    // HOST_NAME_MAX = 64.
#  ifndef HOST_NAME_MAX
#    ifdef _POSIX_HOST_NAME_MAX
#      define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#    else
#      define HOST_NAME_MAX 255
#    endif
#  endif
    char buf[HOST_NAME_MAX + 1] = {0};
    if (::gethostname(buf, sizeof(buf) - 1) == 0) {
        return std::string(buf);
    }
    return {};
#endif
}

LocalWallClock now_local() {
    using clock = std::chrono::system_clock;
    auto tp  = clock::now();
    auto sec = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   tp - sec).count();

    std::time_t t = clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    ::localtime_s(&tm, &t);
#else
    ::localtime_r(&t, &tm);
#endif

    char date_buf[16] = {0};
    std::snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    char time_buf[16] = {0};
    std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec);

    LocalWallClock out;
    out.date      = date_buf;
    out.time      = time_buf;
    out.ms_of_day = static_cast<std::int32_t>(
        ((tm.tm_hour * 3600) + (tm.tm_min * 60) + tm.tm_sec) * 1000 + ms);
    return out;
}

} // namespace openads::platform
