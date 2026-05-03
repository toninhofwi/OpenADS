#pragma once

#include "platform/file.h"
#include "util/result.h"
#include "util/span.h"

#include <cstddef>
#include <cstdint>

namespace openads::platform {

class FileMap {
public:
    FileMap() = default;
    FileMap(const FileMap&) = delete;
    FileMap& operator=(const FileMap&) = delete;
    FileMap(FileMap&&) noexcept;
    FileMap& operator=(FileMap&&) noexcept;
    ~FileMap();

    static util::Result<FileMap> map_readonly(File& f, std::uint64_t offset,
                                              std::size_t length);

    util::Span<const std::uint8_t> bytes() const noexcept {
        return {reinterpret_cast<const std::uint8_t*>(view_), length_};
    }

    // Internal: see ByteLock for rationale on a public construction
    // helper. Always go through `map_readonly`.
    FileMap(void* mapping, void* view, std::size_t length) noexcept
        : mapping_(mapping), view_(view), length_(length) {}

private:
    void unmap_() noexcept;

    void*       mapping_ = nullptr; // Win32: HANDLE; POSIX: unused
    void*       view_    = nullptr;
    std::size_t length_  = 0;
};

} // namespace openads::platform
