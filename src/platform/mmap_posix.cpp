#ifndef _WIN32

#include "platform/mmap.h"

#include <cerrno>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = 5000;
    e.sub_code = errno;
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
        ::munmap(view_, length_);
        view_ = nullptr;
    }
    length_ = 0;
}

util::Result<FileMap> FileMap::map_readonly(File& f, std::uint64_t offset,
                                            std::size_t length) {
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(f.native_handle()));
    void* v = ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd,
                     static_cast<off_t>(offset));
    if (v == MAP_FAILED) return os_error("mmap");
    return FileMap{nullptr, v, length};
}

} // namespace openads::platform

#endif // !_WIN32
