#include "doctest.h"
#include "platform/proc.h"

#include <cstdint>

using openads::platform::process_rss_bytes;

TEST_CASE("process_rss_bytes returns non-zero on a running process") {
    std::uint64_t rss = process_rss_bytes();
    CHECK(rss > 0);
}

TEST_CASE("process_rss_bytes is reasonable range (> 1MB and < 1GB)") {
    std::uint64_t rss = process_rss_bytes();
    // Any C++ test binary uses at least some memory
    CHECK(rss > 1024 * 1024);        // > 1 MB
    CHECK(rss < 1024ULL * 1024 * 1024); // < 1 GB
}

TEST_CASE("process_rss_bytes is idempotent across calls") {
    std::uint64_t first = process_rss_bytes();
    std::uint64_t second = process_rss_bytes();
    // Both calls should return valid values; they may differ slightly
    // but both should be non-zero and in the same order of magnitude.
    CHECK(first > 0);
    CHECK(second > 0);
    // Allow up to 2x variation (e.g. due to lazy allocation)
    CHECK(second >= first / 2);
    CHECK(second <= first * 2);
}
