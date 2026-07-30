#pragma once

#include "trace_replay/core/Assert.hpp"
#include "trace_replay/core/Error.hpp"

#include <optional>
#include <utility>
#include <variant>

namespace trace_replay {

// ============================================================================
// Result<T> — error-propagation carrier
//
// Aligned with Cubium spec 7: no exceptions; use Result to carry a value or an
// Error.
//   Result<void>        indicates only success/failure, carries no value
//   Result<T>           carries a success value or an Error
// The TR_TRY / TR_TRY_VOID macros implement short-circuit error propagation,
// avoiding repetitive layered if-checks. These two macros deliberately use a
// "declarative + return falls into the caller's scope" style and do not rely on
// GNU statement expressions, so they are portable across MSVC/Clang/GCC.
// ============================================================================

template <typename T>
class Result {
public:
    Result(T value) : m_storage(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : m_storage(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool success() const noexcept { return m_storage.index() == 0; }
    [[nodiscard]] bool ok() const noexcept { return success(); }
    [[nodiscard]] explicit operator bool() const noexcept { return success(); }

    [[nodiscard]] const T& value() const& { return std::get<0>(m_storage); }
    [[nodiscard]] T&       value() &      { return std::get<0>(m_storage); }
    [[nodiscard]] T&&      value() &&     { return std::get<0>(std::move(m_storage)); }

    [[nodiscard]] const Error& error() const& { return std::get<1>(m_storage); }
    [[nodiscard]] Error&&      error() &&     { return std::get<1>(std::move(m_storage)); }

private:
    std::variant<T, Error> m_storage;
};

// Result<void> specialization: carries no value, only success/failure
template <>
class Result<void> {
public:
    Result() = default;                       // success
    Result(Error error) : m_error(std::move(error)) {}

    [[nodiscard]] bool success() const noexcept { return !m_error.has_value(); }
    // Note: no bool ok() member is provided — it would clash with the static
    // Result<void>::ok() below of the same name; callers use success() or
    // operator bool to test for success.
    [[nodiscard]] explicit operator bool() const noexcept { return success(); }

    [[nodiscard]] const Error& error() const& { return *m_error; }
    [[nodiscard]] Error&&      error() &&     { return std::move(*m_error); }

    [[nodiscard]] static Result<void> ok() { return {}; }

private:
    std::optional<Error> m_error;
};

// ---------------------------------------------------------------------------
// Error-propagation macros (portable; do not use GNU statement expressions)
// ---------------------------------------------------------------------------

/// TR_TRY_VOID(expr): short-circuits a Result<void>. Equivalent to
///   TRY(createWindow());  in the spec example — on failure it returns that
/// Error from the current function.
#define TR_TRY_VOID(expr) \
    do { \
        auto _tr_result = (expr); \
        if (!_tr_result.success()) { \
            return std::move(_tr_result).error(); \
        } \
    } while (false)

/// TR_TRY(name, expr): unwraps a Result<T> and short-circuits.
///   TR_TRY(chunk, loadChunk(pos));   // on failure return Error; on success
///                                    // get chunk
/// On failure it returns Error from the current function; on success it
/// declares a variable `name` in the current scope holding the value.
#define TR_TRY(name, expr) \
    auto _tr_result_##name = (expr); \
    if (!_tr_result_##name.success()) { \
        return std::move(_tr_result_##name).error(); \
    } \
    auto name = std::move(_tr_result_##name).value()

}  // namespace trace_replay
