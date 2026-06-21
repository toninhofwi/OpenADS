#include "abi/backend_registry.h"

#include <array>
#include <cstddef>

namespace openads::abi {

namespace {
// Fixed table indexed by HandleKind (enum is dense; current max value is 11).
std::array<const BackendTableOps*, 32>& ops_table() {
    static std::array<const BackendTableOps*, 32> t{};  // all nullptr
    return t;
}
}  // namespace

void register_backend_table_ops(openads::session::HandleKind kind,
                                const BackendTableOps* ops) {
    auto idx = static_cast<std::size_t>(kind);
    if (idx < ops_table().size()) ops_table()[idx] = ops;
}

const BackendTableOps* ops_for_kind(openads::session::HandleKind kind) {
    auto idx = static_cast<std::size_t>(kind);
    return idx < ops_table().size() ? ops_table()[idx] : nullptr;
}

}  // namespace openads::abi
