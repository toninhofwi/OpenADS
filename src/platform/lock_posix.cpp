#ifndef _WIN32

#include "platform/lock.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = (errno == EAGAIN || errno == EACCES) ? 5012 : 5013;
    e.sub_code = errno;
    e.message  = op;
    return e;
}

// Prefer OFD (open-file-description) locks where available
// (Linux >= 3.15). Plain F_SETLK locks are process-scoped, which
// breaks the ByteLock contract that two fds in the SAME process
// should still contend (Win32 LockFile is fd-scoped). OFD locks
// are tied to the open file description, matching the Win32
// semantics used by the engine.
#ifdef F_OFD_SETLK
constexpr int kSetLk  = F_OFD_SETLK;
constexpr int kSetLkW = F_OFD_SETLKW;
#else
constexpr int kSetLk  = F_SETLK;
constexpr int kSetLkW = F_SETLKW;
#endif

util::Result<ByteLock> do_lock(File& f, std::uint64_t offset,
                               std::uint64_t length, LockKind kind,
                               int cmd) {
    struct flock fl{};
    fl.l_type   = (kind == LockKind::Exclusive) ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = static_cast<off_t>(offset);
    fl.l_len    = static_cast<off_t>(length);
    fl.l_pid    = 0;            // OFD requires l_pid=0
    // native_handle() stores (fd + 1) to avoid the nullptr/fd-0 collision.
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(f.native_handle()) - 1);
    if (::fcntl(fd, cmd, &fl) == -1) return os_error("fcntl(F_SETLK)");
    return ByteLock{f.native_handle(), offset, length};
}

} // namespace

ByteLock::ByteLock(ByteLock&& other) noexcept
    : native_(other.native_), offset_(other.offset_), length_(other.length_) {
    other.native_ = nullptr;
}

ByteLock& ByteLock::operator=(ByteLock&& other) noexcept {
    if (this != &other) {
        release_();
        native_ = other.native_;
        offset_ = other.offset_;
        length_ = other.length_;
        other.native_ = nullptr;
    }
    return *this;
}

ByteLock::~ByteLock() { release_(); }

void ByteLock::release_() noexcept {
    if (native_ == nullptr) return;
    struct flock fl{};
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = static_cast<off_t>(offset_);
    fl.l_len    = static_cast<off_t>(length_);
    fl.l_pid    = 0;
    // native_handle() stores (fd + 1) to avoid the nullptr/fd-0 collision.
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(native_) - 1);
    ::fcntl(fd, kSetLk, &fl);
    native_ = nullptr;
}

util::Result<ByteLock> ByteLock::acquire(File& f, std::uint64_t offset,
                                         std::uint64_t length, LockKind kind) {
    return do_lock(f, offset, length, kind, kSetLkW);
}

util::Result<ByteLock> ByteLock::try_acquire(File& f, std::uint64_t offset,
                                             std::uint64_t length,
                                             LockKind kind) {
    return do_lock(f, offset, length, kind, kSetLk);
}

util::Result<void> ByteLock::release() {
    release_();
    return {};
}

} // namespace openads::platform

#endif // !_WIN32
