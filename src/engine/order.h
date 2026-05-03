#pragma once

#include "drivers/index_trait.h"

#include <memory>
#include <optional>
#include <string>

namespace openads::engine {

struct Scope {
    std::optional<std::string> top;
    std::optional<std::string> bottom;
};

class Order {
public:
    Order() = default;
    explicit Order(std::unique_ptr<drivers::IIndex> idx) noexcept
        : index_(std::move(idx)) {}
    Order(Order&&) noexcept = default;
    Order& operator=(Order&&) noexcept = default;

    drivers::IIndex* index() noexcept { return index_.get(); }
    const drivers::IIndex* index() const noexcept { return index_.get(); }

    Scope&       scope()       noexcept { return scope_; }
    const Scope& scope() const noexcept { return scope_; }

private:
    std::unique_ptr<drivers::IIndex> index_;
    Scope                            scope_;
};

} // namespace openads::engine
