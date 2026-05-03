#ifdef _WIN32

#include "platform/mmap.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = 5000;
    e.sub_code = static_cast<std::int32_t>(::GetLastError());
    e.message  = op;
    return e;
}

} // namespace

FileMap::FileMap(FileMap&& other) noexcept
    : mapping_(other.mapping_), view_(other.view_), length_(other.length_) {
    other.mapping_ = nullptr;
    other.view_    = nullptr;
    other.length_  = 0;
}

FileMap& FileMap::operator=(FileMap&& other) noexcept {
    if (this != &other) {
        unmap_();
        mapping_ = other.mapping_;
        view_    = other.view_;
        length_  = other.length_;
        other.mapping_ = nullptr;
        other.view_    = nullptr;
        other.length_  = 0;
    }
    return *this;
}

FileMap::~FileMap() { unmap_(); }

void FileMap::unmap_() noexcept {
    if (view_) {
        ::UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_) {
        ::CloseHandle(reinterpret_cast<HANDLE>(mapping_));
        mapping_ = nullptr;
    }
    length_ = 0;
}

util::Result<FileMap> FileMap::map_readonly(File& f, std::uint64_t offset,
                                            std::size_t length) {
    HANDLE h = reinterpret_cast<HANDLE>(f.native_handle());
    HANDLE m = ::CreateFileMappingA(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) return os_error("CreateFileMappingA");
    DWORD lo = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    DWORD hi = static_cast<DWORD>(offset >> 32);
    void* v = ::MapViewOfFile(m, FILE_MAP_READ, hi, lo, length);
    if (!v) { ::CloseHandle(m); return os_error("MapViewOfFile"); }
    return FileMap{m, v, length};
}

} // namespace openads::platform

#endif // _WIN32
