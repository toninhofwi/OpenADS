#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace openads::util {

struct Error {
    std::int32_t code = 0;
    std::int32_t sub_code = 0;
    std::string  message;
    std::string  context;
};

template <class T>
class Result {
public:
    using value_type = T;

    Result(T v) : data_(std::move(v)) {}
    Result(Error e) : data_(std::move(e)) {}

    bool has_value() const noexcept {
        return std::holds_alternative<T>(data_);
    }
    explicit operator bool() const noexcept { return has_value(); }

    T&        value() &       { return std::get<T>(data_); }
    const T&  value() const & { return std::get<T>(data_); }
    T&&       value() &&      { return std::move(std::get<T>(data_)); }

    Error&        error() &       { return std::get<Error>(data_); }
    const Error&  error() const & { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

template <>
class Result<void> {
public:
    Result() : err_() {}
    Result(Error e) : err_(std::move(e)) {}

    bool has_value() const noexcept { return err_.code == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    const Error& error() const noexcept { return err_; }

private:
    Error err_;
};

} // namespace openads::util

// Try-macro: evaluates expr, returns its error from the enclosing
// function if it failed, otherwise binds the value to `decl`.
//
// __LINE__ needs two layers of expansion before token-pasting so the
// generated identifier is, e.g., `_openads_try_42` and not the literal
// `_openads_try___LINE__`.
#define OPENADS_TRY_CAT2(a, b) a##b
#define OPENADS_TRY_CAT(a, b)  OPENADS_TRY_CAT2(a, b)
#define OPENADS_TRY_VAR        OPENADS_TRY_CAT(_openads_try_, __LINE__)

#define OPENADS_TRY(decl, expr)                                          \
    auto OPENADS_TRY_VAR = (expr);                                       \
    if (!OPENADS_TRY_VAR) {                                              \
        return OPENADS_TRY_VAR.error();                                  \
    }                                                                    \
    decl = std::move(OPENADS_TRY_VAR).value()
