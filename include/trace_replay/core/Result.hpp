#pragma once

#include "trace_replay/core/Assert.hpp"
#include "trace_replay/core/Error.hpp"

#include <optional>
#include <utility>
#include <variant>

namespace trace_replay {

// ============================================================================
// Result<T> —— 错误传播载体
//
// 对齐 Cubium 规范 7：禁止异常，用 Result 携带值或 Error。
//   Result<void>        仅表示成功/失败，不携带值
//   Result<T>           携带成功值或 Error
// 用 TR_TRY / TR_TRY_VOID 宏实现错误短路传播，避免层层 if 重复。
// 这两个宏刻意采用"声明式 + return 落在调用者作用域"的写法，不依赖
// GNU statement expression，因此在 MSVC/Clang/GCC 上均可移植。
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

// Result<void> 特化：不携带值，仅成功/失败
template <>
class Result<void> {
public:
    Result() = default;                       // 成功
    Result(Error error) : m_error(std::move(error)) {}

    [[nodiscard]] bool success() const noexcept { return !m_error.has_value(); }
    // 注意：不提供 bool ok() 成员，与下方静态 Result<void>::ok() 同名会冲突；
    // 调用方用 success() 或 operator bool 判定成功。
    [[nodiscard]] explicit operator bool() const noexcept { return success(); }

    [[nodiscard]] const Error& error() const& { return *m_error; }
    [[nodiscard]] Error&&      error() &&     { return std::move(*m_error); }

    [[nodiscard]] static Result<void> ok() { return {}; }

private:
    std::optional<Error> m_error;
};

// ---------------------------------------------------------------------------
// 错误传播宏（可移植，不使用 GNU statement expression）
// ---------------------------------------------------------------------------

/// TR_TRY_VOID(expr)：对 Result<void> 做错误短路。等价于规范示例中的
///   TRY(createWindow());  —— 失败即从当前函数返回该 Error。
#define TR_TRY_VOID(expr) \
    do { \
        auto _tr_result = (expr); \
        if (!_tr_result.success()) { \
            return std::move(_tr_result).error(); \
        } \
    } while (false)

/// TR_TRY(name, expr)：对 Result<T> 解值并短路。
///   TR_TRY(chunk, loadChunk(pos));   // 失败则 return Error，成功则得到 chunk
/// 失败时从当前函数返回 Error；成功时在当前作用域声明变量 name 持有值。
#define TR_TRY(name, expr) \
    auto _tr_result_##name = (expr); \
    if (!_tr_result_##name.success()) { \
        return std::move(_tr_result_##name).error(); \
    } \
    auto name = std::move(_tr_result_##name).value()

}  // namespace trace_replay
