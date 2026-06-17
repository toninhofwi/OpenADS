#pragma once

#include "util/result.h"

#include <cstddef>
#include <string>

namespace openads::platform {

// M11.4 — minimal cross-platform DLL loader for the AEP host.
struct DllHandle {
    void* native = nullptr;
};

inline bool dll_valid(DllHandle h) noexcept { return h.native != nullptr; }

util::Result<DllHandle> dll_load(const std::string& path);
util::Result<void*>     dll_symbol(DllHandle h, const std::string& name);
void                    dll_close(DllHandle h) noexcept;

// Probe whether the DLL at `path` is OpenADS's own ACE-compatible engine
// (not SAP's). Loads the DLL, calls AdsGetVersion, checks the description
// string for "OpenADS", then immediately unloads. Returns the description
// on success (non-empty → is OpenADS), empty string if the DLL cannot be
// loaded, lacks AdsGetVersion, or identifies as SAP Advantage.
std::string dll_probe_ace(const std::string& path) noexcept;

} // namespace openads::platform
