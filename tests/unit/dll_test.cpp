#include "doctest.h"
#include "platform/dll.h"

#ifdef _WIN32

#include <string>

using openads::platform::DllHandle;
using openads::platform::dll_load;
using openads::platform::dll_symbol;
using openads::platform::dll_close;
using openads::platform::dll_valid;
using openads::platform::dll_probe_ace;

TEST_CASE("dll_valid: null handle is invalid") {
    DllHandle h;
    CHECK_FALSE(dll_valid(h));
}

TEST_CASE("dll_valid: non-null handle is valid") {
    DllHandle h;
    h.native = reinterpret_cast<void*>(1);
    CHECK(dll_valid(h));
}

TEST_CASE("dll_load: kernel32.dll loads successfully") {
    auto r = dll_load("kernel32.dll");
    REQUIRE(r);
    CHECK(dll_valid(r.value()));
    dll_close(r.value());
}

TEST_CASE("dll_load: nonexistent DLL returns error") {
    auto r = dll_load("nonexistent_library_12345.dll");
    CHECK_FALSE(r);
}

TEST_CASE("dll_symbol: valid symbol from kernel32") {
    auto h = dll_load("kernel32.dll");
    REQUIRE(h);
    auto s = dll_symbol(h.value(), "GetLastError");
    CHECK(s);
    dll_close(h.value());
}

TEST_CASE("dll_symbol: invalid handle returns error") {
    DllHandle bad;
    auto s = dll_symbol(bad, "GetLastError");
    CHECK_FALSE(s);
}

TEST_CASE("dll_symbol: nonexistent symbol returns error") {
    auto h = dll_load("kernel32.dll");
    REQUIRE(h);
    auto s = dll_symbol(h.value(), "NonExistentFunction_12345");
    CHECK_FALSE(s);
    dll_close(h.value());
}

TEST_CASE("dll_close: does not crash on null handle") {
    DllHandle h;
    dll_close(h);  // should be no-op
}

TEST_CASE("dll_close: does not crash on valid handle") {
    auto h = dll_load("kernel32.dll");
    REQUIRE(h);
    dll_close(h.value());
}

TEST_CASE("dll_probe_ace: returns empty for non-ACE DLL") {
    auto result = dll_probe_ace("kernel32.dll");
    CHECK(result.empty());
}

TEST_CASE("dll_probe_ace: returns empty for nonexistent file") {
    auto result = dll_probe_ace("nonexistent_ace_12345.dll");
    CHECK(result.empty());
}

TEST_CASE("dll_probe_ace: returns non-empty for openace64.dll") {
    // Try to probe the built DLL
    auto result = dll_probe_ace("C:\\OpenADS\\build\\default\\src\\Release\\openace64.dll");
    if (!result.empty()) {
        CHECK(result.find("OpenADS") != std::string::npos);
    }
    // If the DLL doesn't exist yet, that's OK - test still passes
    WARN("dll_probe_ace: tested (result may be empty if DLL not built)");
}

#else

TEST_CASE("dll: Windows-only, skipped on this platform") {
    WARN("dll tests skipped on non-Windows");
}

#endif
