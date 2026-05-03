#include "doctest.h"
#include "util/span.h"

#include <array>
#include <cstdint>
#include <vector>

using openads::util::Span;

TEST_CASE("Span over std::vector exposes size and elements") {
    std::vector<int> v{1, 2, 3, 4};
    Span<int> s(v.data(), v.size());
    CHECK(s.size() == 4);
    CHECK(s[0] == 1);
    CHECK(s[3] == 4);
}

TEST_CASE("Span supports range-for") {
    std::array<std::uint8_t, 3> a{10, 20, 30};
    Span<std::uint8_t> s(a.data(), a.size());
    int total = 0;
    for (auto byte : s) total += byte;
    CHECK(total == 60);
}

TEST_CASE("Empty Span is empty") {
    Span<int> s(nullptr, 0);
    CHECK(s.empty());
    CHECK(s.size() == 0);
}

TEST_CASE("subspan returns a slice") {
    int data[] = {0, 1, 2, 3, 4};
    Span<int> s(data, 5);
    auto tail = s.subspan(2);
    CHECK(tail.size() == 3);
    CHECK(tail[0] == 2);
    auto mid = s.subspan(1, 3);
    CHECK(mid.size() == 3);
    CHECK(mid[2] == 3);
}
