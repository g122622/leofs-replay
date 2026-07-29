#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/Types.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace trace_replay {

// ============================================================================
// fd 表 —— openat-replay 的核心
//
// 真实 rawproc 输出（已验证）的 fd 语义：
//   * openat 的 ret = 原始 fd（小整数）；arg1 = flags（十进制）。
//   * read/write 的 arg1 = 请求字节数 count（不是 fd），fd 信息不在 arg。
//     其 path 列绝大多数是已解析的完整路径，少数为 "/[unknown, fd=N]"。
//   * close 的 arg1/ret 均为 0，fd 同样靠 path。
//
// 因此 fd 跟踪策略：
//   * openat 成功 → 用 (pid, origFd=ret) 建表，并记录 path；
//   * read/write/close → 优先用 path（canonical_path 优先）在同 pid 表里
//     反查 ourFd（同路径即同 fd）；path 为 "/[unknown, fd=N]" 时从其中
//     提取 N 直查 (pid, N)。
//
// 维护两张索引：fd 索引（origFd→entry）与 path 索引（canonicalPath→entry），
// openat 注册、close 注销时同步更新两者。path 索引允许同一路径被多次打开
// （多个 fd 指向同路径），取最近一次登记的 ourFd。
// ============================================================================

/**
 * @brief 单条 fd 映射条目
 */
struct FdEntry {
    Fd  ourFd {-1};          // 本回放进程真实打开的 fd（-1 表示未占用）
    std::string path;        // canonical_path（沙箱解析前的原始归一路径），用于反查
    bool isDirectory {false};
};

/**
 * @brief per-pid fd 映射表，支持 fd 直查与 path 反查
 */
class FdTable {
public:
    FdTable() = default;

    /**
     * @brief 登记一次成功的 open
     *
     * @param pid      原始进程 id
     * @param origFd   原始 trace 中 openat 返回的 fd（即该 fd 的查找键）
     * @param ourFd    本进程真实打开得到的 fd
     * @param path     canonical_path（用于 IO/close 反查）
     * @param isDir    是否目录
     */
    void registerFd(i64 pid, Fd origFd, Fd ourFd, std::string path, bool isDir);

    /**
     * @brief 注销一个原始 fd（fd 直查路径，用于 "/[unknown, fd=N]"）
     *
     * @return 本进程 ourFd（供调用方关闭），nullopt 表示该 (pid,origFd) 未登记
     */
    [[nodiscard]] std::optional<Fd> unregisterFd(i64 pid, Fd origFd);

    /**
     * @brief 按 path 反查并注销（用于 path 已解析的 close）
     *
     * 取该 pid 下最近一次登记该 path 的 ourFd 并注销。返回 ourFd 供关闭；
     * nullopt 表示该 (pid,path) 未登记。
     */
    [[nodiscard]] std::optional<Fd> unregisterByPath(i64 pid, std::string_view path);

    /// 按 fd 直查条目（IO/close 在 "/[unknown, fd=N]" 时使用）
    [[nodiscard]] const FdEntry* lookup(i64 pid, Fd origFd) const;

    /// 按 path 反查条目（IO/close 在 path 已解析时使用）
    [[nodiscard]] const FdEntry* lookupByPath(i64 pid, std::string_view path) const;

    /// 关闭并清空某个 pid 的全部映射（进程退出时调用）
    void closePid(i64 pid);

    /// 当前登记的映射总数（诊断用）
    [[nodiscard]] size_t size() const noexcept;

private:
    struct PerPid {
        std::unordered_map<Fd, FdEntry> byFd;
        // path → 最近一次登记的 origFd（指向 byFd 中的条目）
        std::unordered_map<std::string, Fd> byPath;
    };
    std::unordered_map<i64, PerPid> m_table;
};

}  // namespace trace_replay
