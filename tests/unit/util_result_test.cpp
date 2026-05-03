#include "doctest.h"
#include "util/result.h"

using openads::util::Error;
using openads::util::Result;

TEST_CASE("Result holds a value when constructed from one") {
    Result<int> r{42};
    CHECK(r.has_value());
    CHECK(r.value() == 42);
    CHECK(static_cast<bool>(r));
}

TEST_CASE("Result holds an error when constructed from one") {
    Result<int> r{Error{5012, 0, "locked", ""}};
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 5012);
    CHECK(r.error().message == "locked");
}

TEST_CASE("Result<void> distinguishes ok from error") {
    Result<void> ok;
    CHECK(ok.has_value());

    Result<void> err{Error{4001, 0, "net", ""}};
    CHECK_FALSE(err.has_value());
    CHECK(err.error().code == 4001);
}

TEST_CASE("OPENADS_TRY propagates errors") {
    auto produce_err = []() -> Result<int> {
        return Error{5004, 0, "not impl", ""};
    };
    auto wrap = [&]() -> Result<int> {
        OPENADS_TRY(int v, produce_err());
        return v + 1;
    };
    auto r = wrap();
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 5004);
}

TEST_CASE("OPENADS_TRY forwards the value when ok") {
    auto produce_ok = []() -> Result<int> { return 7; };
    auto wrap = [&]() -> Result<int> {
        OPENADS_TRY(int v, produce_ok());
        return v + 1;
    };
    auto r = wrap();
    REQUIRE(r.has_value());
    CHECK(r.value() == 8);
}
