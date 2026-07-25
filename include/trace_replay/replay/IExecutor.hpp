#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/model/FdTable.hpp"
#include "trace_replay/model/PathResolver.hpp"

namespace trace_replay {

// ============================================================================
// 事件执行器接口
//
// replay 引擎从 EventMerger 拿到全局时间序事件后，交给 IExecutor"执行"。
// 两个实现：
//   * DryRunExecutor —— 不碰真实 syscall，仅按 fd 表逻辑推演并打印/统计
//   * SyscallExecutor—— 在沙箱内真实执行 syscall（openat/read/write/...）
//
// 执行器内部持有 FdTable 与 PathResolver，以 fd 为准把原始操作映射到本进程。
// execute() 返回该事件执行结果（成功/失败+原因），由引擎决定是否继续。
// ============================================================================

/// 单条事件执行结果
struct ExecOutcome {
    bool   ok {true};
    bool   skipped {false};   // 因过滤/未知 op 等被跳过（非错误）
    std::string note;         // 诊断信息（如失败原因、跳过原因）
};

class IExecutor {
public:
    virtual ~IExecutor() = default;

    /**
     * @brief 执行单条事件
     *
     * 实现须自行处理：
     *   * openat/open → 解析路径、真实打开、登记 fd 表
     *   * read/write/... → 查 fd 表得 ourFd，对本进程 ourFd 执行
     *   * close → 查 fd 表、真实关闭、注销
     *   * stat/rename/mkdir/... → 解析路径、真实执行
     *   * 其它未知 syscall → skipped
     */
    [[nodiscard]] virtual Result<ExecOutcome> execute(const TraceEvent& ev) = 0;

    /// 执行器内部 fd 表（引擎用于诊断/统计）
    [[nodiscard]] virtual const FdTable& fdTable() const noexcept = 0;
};

}  // namespace trace_replay
