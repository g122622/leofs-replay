#pragma once

#include "trace_replay/replay/IExecutor.hpp"

#include <filesystem>

namespace trace_replay {

// ============================================================================
// Syscall 执行器 —— 真实执行
//
// 在受限沙箱根目录下对真实文件系统执行 syscall，是 replay 的核心语义。
// 关键：以 fd 为准。IO 操作只信赖 (pid, arg1=fd)，经 fd 表映射到本进程真实
// 打开的 fd，再对本进程 fd 执行 read/write 等。
//
// 路径防穿越由 PathResolver 保障：所有解析出的路径都强制落在 sandboxRoot 下。
//
// 说明：原始 trace 中 read/write 的 ret 表示"实际读写字节数"。我们无法复现
// 原始数据内容（trace 不含数据负载），故对 read 用零填充缓冲、对 write 写出
// ret 字节的占位数据，目的是还原"该 fd 上发生了 N 字节 IO"的时间序列语义，
// 而非还原数据本身。这一点在 README 中明示。
// ============================================================================

class SyscallExecutor final : public IExecutor {
public:
    SyscallExecutor(std::filesystem::path sandboxRoot, i64 maxIoBytes,
                    bool continueOnError, std::ostream& log);

    [[nodiscard]] Result<ExecOutcome> execute(const TraceEvent& ev) override;
    [[nodiscard]] const FdTable& fdTable() const noexcept override { return m_fdTable; }

private:
    [[nodiscard]] Result<ExecOutcome> doOpen(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doIo(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doClose(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doStatLike(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doMkdir(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doUnlink(const TraceEvent& ev);
    [[nodiscard]] Result<ExecOutcome> doRename(const TraceEvent& ev);

    FdTable       m_fdTable;
    PathResolver  m_resolver;
    i64           m_maxIoBytes;
    bool          m_continueOnError;
    std::ostream& m_log;
};

}  // namespace trace_replay
