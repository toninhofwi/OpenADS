#include "doctest.h"

// legacy_crt_shims symbols (openads_dclass, openads_dsign) live in
// openads_ace.dll, not openads_core.  We can only test them when
// the test executable links against the ace DLL, which the default
// CMake config does not.  Guard with a feature-test: if the symbols
// are available (linked via ace64.dll import lib), run the tests;
// otherwise skip cleanly.

#if defined(_WIN32) && defined(OPENADS_TEST_LINKS_ACE)

#include <cmath>
#include <cstdint>
#include <limits>

extern "C" {
int openads_dclass(double x);
int openads_dsign(double x);
}

TEST_CASE("openads_dclass: classifies NaN") {
    CHECK(openads_dclass(std::nan("")) != 0);
}

TEST_CASE("openads_dclass: classifies +Infinity") {
    CHECK(openads_dclass(std::numeric_limits<double>::infinity()) != 0);
}

TEST_CASE("openads_dclass: classifies -Infinity") {
    CHECK(openads_dclass(-std::numeric_limits<double>::infinity()) != 0);
}

TEST_CASE("openads_dclass: classifies zero") {
    CHECK(openads_dclass(0.0) != 0);
}

TEST_CASE("openads_dclass: classifies normal number") {
    CHECK(openads_dclass(3.14) != 0);
}

TEST_CASE("openads_dsign: positive number returns 0") {
    CHECK(openads_dsign(1.0) == 0);
}

TEST_CASE("openads_dsign: negative number returns 1") {
    CHECK(openads_dsign(-1.0) == 1);
}

TEST_CASE("openads_dsign: zero returns 0") {
    CHECK(openads_dsign(0.0) == 0);
}

TEST_CASE("openads_dsign: positive zero returns 0") {
    CHECK(openads_dsign(+0.0) == 0);
}

TEST_CASE("openads_dsign: negative zero returns 1") {
    CHECK(openads_dsign(-0.0) == 1);
}

TEST_CASE("openads_dsign: NaN sign") {
    int r = openads_dsign(std::nan(""));
    CHECK((r == 0 || r == 1));
}

#else

TEST_CASE("legacy_crt_shims: requires _WIN32 + ACE link, skipping") {
    WARN("legacy_crt_shims: skipped (not linked against ace)");
}

#endif
