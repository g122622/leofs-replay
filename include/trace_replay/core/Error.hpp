#pragma once

#include <string>
#include <string_view>

namespace trace_replay {

// ============================================================================
// 错误类型
//
// 对齐 Cubium 规范 7：用 Result/Error 传播错误，禁止异常。错误携带
//   code    —— 粗分类，便于调用方按类别处理
//   message —— 人类可读描述
//   source  —— 出错位置（一般是 "Class::method"），便于定位
// ============================================================================

/**
 * @brief 错误对象，可被 Result 携带传播
 */
class Error {
public:
    /// 错误粗分类
    enum class Code {
        Ok = 0,
        NotFound,           // 文件/桶/列不存在
        InvalidArgument,    // 参数非法（空路径、非法配置等）
        InvalidFormat,      // trace 行/列格式无法解析
        OutOfRange,         // 下标越界、数值越界
        IOError,            // 文件/Parquet 读写失败
        Syscall,            // replay 时真实 syscall 失败
        Internal,           // 内部不变量被破坏
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

    /// 拼成单行字符串，便于日志输出
    [[nodiscard]] std::string toString() const;

    // 常用工厂方法
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
