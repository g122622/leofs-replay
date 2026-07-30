#pragma once

#include "trace_replay/replay/IExecutor.hpp"

#include <filesystem>

namespace trace_replay {

// ============================================================================
// Dry-run executor
//
// Executes no real syscalls; only simulates the fd table and paths following the
// "fd is authoritative" logic, and prints each event. Used to verify that the
// parsing, sorting, and fd-mapping logic is correct; safe and repeatable.
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
    Fd            m_nextDryFd {3};   // dry-run virtual fd counter, starting at 3 (avoiding 0/1/2)
};

}  // namespace trace_replay
