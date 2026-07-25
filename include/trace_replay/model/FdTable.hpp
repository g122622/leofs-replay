#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/Types.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace trace_replay {

// ============================================================================
// fd 表 —— openat-replay 的核心，"以 fd 为准而非 path"
//
// 原始 trace 中，IO 操作（read/write/...）的 path 列不可信甚至为空，真正
// 携带文件信息的是 (pid, arg1=fd)。本表维护"原始 trace 的 fd"到"我们回放
// 进程真实打开的 fd"的映射：
//
//   原始 (pid, fd)  ──查表──>  本进程 fd + 路径  ──>  对本进程 fd 执行真实 syscall
//
// 生命周期：
//   * openat/open 成功 → 登记 (pid, origFd) → (ourFd, path)
//   * close(origFd)    → 注销，并真实关闭 ourFd
//   * dup/dup2/dup3    → 复制映射（TODO: 当前未覆盖，按需补）
//   * 进程退出（exit）  → 关闭该 pid 全部 ourFd（TODO: 依赖 exit 事件，暂按需）
//
// 注意：原始 trace 的 ret 即 openat 返回的 fd（成功时）。我们以"原始 ret fd"
// 作为该次 open 在后续 IO 中的查找键；而我们真实 openat 得到的 fd 存为 ourFd。
// ============================================================================

/**
 * @brief 单条 fd 映射条目
 */
struct FdEntry {
    Fd  ourFd {-1};          // 本回放进程真实打开的 fd（-1 表示未占用）
    std::string path;        // 该 fd 对应的沙箱内绝对路径（诊断/防穿越用）
    bool isDirectory {false};// 是否为目录 fd（getdents 等需要）
};

/**
 * @brief per-pid fd 映射表
 */
class FdTable {
public:
    FdTable() = default;

    /**
     * @brief 登记一次成功的 open：原始 fd 关联到本进程 ourFd
     *
     * @param pid      原始进程 id
     * @param origFd   原始 trace 中 openat 返回的 fd（即该 IO 的查找键）
     * @param ourFd    本进程真实打开得到的 fd
     * @param path     沙箱内绝对路径
     * @param isDir    是否目录
     */
    void registerFd(i64 pid, Fd origFd, Fd ourFd, std::string path, bool isDir);

    /**
     * @brief 注销并真实关闭一个原始 fd
     *
     * @return 本进程 ourFd（供调用方关闭），nullopt 表示该 (pid,origFd) 未登记
     */
    [[nodiscard]] std::optional<Fd> unregisterFd(i64 pid, Fd origFd);

    /**
     * @brief 查询 (pid, origFd) 对应的条目
     *
     * replay IO 操作时以此为准：拿到 ourFd 后对本进程 ourFd 执行 read/write。
     */
    [[nodiscard]] const FdEntry* lookup(i64 pid, Fd origFd) const;

    /// 关闭并清空某个 pid 的全部映射（进程退出时调用）
    void closePid(i64 pid);

    /// 当前登记的映射总数（诊断用）
    [[nodiscard]] size_t size() const noexcept;

private:
    // pid → (origFd → entry)
    std::unordered_map<i64, std::unordered_map<Fd, FdEntry>> m_table;
};

}  // namespace trace_replay
