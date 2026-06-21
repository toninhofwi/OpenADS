#pragma once

#include "openads/ace.h"
#include "abi/backend_table_ops.h"
#include "session/handle_registry.h"

namespace openads::abi {

// Register a backend's ops under its table HandleKind (e.g. SqliteTable).
void register_backend_table_ops(openads::session::HandleKind kind,
                                const BackendTableOps* ops);

// Look up ops by kind. Returns nullptr for unregistered kinds (native/remote).
const BackendTableOps* ops_for_kind(openads::session::HandleKind kind);

// Resolve a live handle to its backend ops, or nullptr. DEFINED IN ace_exports.cpp
// (Task 3) because it needs the process session registry. Declared here for callers.
const BackendTableOps* backend_table_ops_for(ADSHANDLE h);

// Registers all compiled-in backends. DEFINED IN ace_exports.cpp (Task 3) — it is
// the single place holding the per-backend #if. Declared here.
void register_builtin_backends();

}  // namespace openads::abi
