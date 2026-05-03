#include "platform/thread.h"

#include <thread>
#include <functional>

namespace openads::platform {

std::uint64_t current_thread_id() {
    return static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

} // namespace openads::platform
