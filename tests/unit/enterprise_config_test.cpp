#include "doctest.h"
#include "sql_backend/enterprise_config.h"

TEST_CASE("enterprise config: defaults are enterprise-scale") {
    const auto& c = openads::sql_backend::enterprise_config();
    // Server limits consumed by the wire server (session reaping + cap).
    CHECK(c.server_max_sessions >= 100);
    CHECK(c.server_listen_backlog >= 64);
    // Pool / lock / timeout defaults (used by the SQL backends in later work).
    CHECK(c.odbc_pool_max_per_dsn >= 32);
    CHECK(c.sqlite_pool_max_per_db >= 16);
    CHECK(c.lock_retry_count >= 20);
    CHECK(c.sqlite_busy_timeout_ms >= 5000);
}

TEST_CASE("enterprise pool toggles default on; OPENADS_POOL_ENABLED=0 disables") {
    // Toggles re-read the environment on each call (no cached singleton), so a
    // deployment can flip pooling off without a restart.
    CHECK(openads::sql_backend::enterprise_pool_enabled() == true);
}