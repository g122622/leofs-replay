#pragma once

#include "trace_replay/replay/IExecutor.hpp"

#include <filesystem>

namespace trace_replay {

// ============================================================================
// Syscall executor — real execution
//
// Executes syscalls against the real filesystem under a restricted sandbox root;
// this is the core semantics of replay. Key point: fd is authoritative. IO
// operations trust only (pid, arg1=fd), mapped through the fd table to the fd
// actually opened by this process, on which read/write etc. are then executed.
//
// Path traversal protection is ensured by PathResolver: all resolved paths are
// forced to stay under sandboxRoot.
//
// Note: in the original trace, the ret of read/write means "actual bytes
// read/written". We cannot reproduce the original data content (the trace
// carries no payload), so read uses a zero-filled buffer and write writes `ret`
// bytes of placeholder data, in order to reproduce the time-series semantics of
// "N bytes of IO occurred on this fd" — not the data itself. This is stated
// explicitly in the README.
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
