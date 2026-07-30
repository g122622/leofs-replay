#pragma once

#include <string>
#include <string_view>

namespace trace_replay {

// ============================================================================
// Error type
//
// Aligned with Cubium spec 7: propagate errors via Result/Error, no exceptions.
// An error carries:
//   code    — coarse category, so callers can handle by category
//   message — human-readable description
//   source  — error location (usually "Class::method"), for quick locating
// ============================================================================

/**
 * @brief Error object, can be carried and propagated by Result
 */
class Error {
public:
    /// Coarse error category
    enum class Code {
        Ok = 0,
        NotFound,           // file/bucket/column does not exist
        InvalidArgument,    // illegal argument (empty path, invalid config, etc.)
        InvalidFormat,      // trace line/column format cannot be parsed
        OutOfRange,         // index out of range, numeric overflow
        IOError,            // file/Parquet read/write failure
        Syscall,            // a real syscall failed during replay
        Internal,           // an internal invariant was broken
    };

    Error() = default;
    Error(Code code, std::string message, std::string source)
        : m_code(code)
        , m_message(std::move(message))
        , m_source(std::move(source)) {}

    [[nodiscard]] Code code() const noexcept { return m_code; }
    [[nodiscard]] bool ok() const noexcept { return m_code == Code::Ok; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }
    [[nodiscard]] const std::string& source() const noexcept { return m_source; }

    /// Join into a single-line string for easy log output
    [[nodiscard]] std::string toString() const;

    // Common factory methods
    [[nodiscard]] static Error invalidArgument(std::string msg, std::string src = {});
    [[nodiscard]] static Error invalidFormat(std::string msg, std::string src = {});
    [[nodiscard]] static Error notFound(std::string msg, std::string src = {});
    [[nodiscard]] static Error ioError(std::string msg, std::string src = {});
    [[nodiscard]] static Error syscallError(std::string msg, std::string src = {});
    [[nodiscard]] static Error internal(std::string msg, std::string src = {});

private:
    Code        m_code {Code::Ok};
    std::string m_message;
    std::string m_source;
};

}  // namespace trace_replay
