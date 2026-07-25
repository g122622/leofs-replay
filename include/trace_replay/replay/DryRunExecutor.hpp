#pragma once

#include "trace_replay/replay/IExecutor.hpp"

#include <filesystem>

namespace trace_replay {

// ============================================================================
// Dry-Run 执行器
//
// 不执行任何真实 syscall，仅按"以 fd 为准"的逻辑推演 fd 表与路径，并打印
// 每条事件。用于校验解析、排序、fd 映射逻辑是否正确，安全可重复。
// ============================================================================

class DryRunExecutor final : public IExecutor {
public:
    DryRunExecutor(std::filesystem::path sandboxRoot, std::ostream& log);

    [[nodiscard]] Result<ExecOutcome> execute(const TraceEvent& ev) override;
    [[nodiscard]] const FdTable& fdTable() const noexcept override { return m_fdTable; }

private:
    [[nodiscard]] Result<ExecOutcome> doOpen(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doIo(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doClose(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doMeta(const TraceEvent& ev);

    FdTable       m_fdTable;
    PathResolver  m_resolver;
    std::ostream& m_log;
    Fd            m_nextDryFd {3};   // dry-run 虚拟 fd 计数器，从 3 起（避开 0/1/2）
};

}  // namespace trace_replay
