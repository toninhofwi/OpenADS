#include "doctest.h"
#include "platform/thread.h"

#include <thread>

using openads::platform::current_thread_id;

TEST_CASE("current_thread_id is non-zero for the main thread") {
    auto id = current_thread_id();
    CHECK(id != 0);
}

TEST_CASE("current_thread_id differs across threads") {
    auto a = current_thread_id();
    std::uint64_t b = 0;
    std::thread t([&] { b = current_thread_id(); });
    t.join();
    CHECK(b != 0);
    CHECK(b != a);
}
