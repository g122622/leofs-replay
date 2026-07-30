#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/model/FdTable.hpp"
#include "trace_replay/model/PathResolver.hpp"

namespace trace_replay {

// ============================================================================
// Event executor interface
//
// After the replay engine obtains a globally time-ordered event from
// EventMerger, it hands it to IExecutor to "execute". Two implementations:
//   * DryRunExecutor — does not touch real syscalls; only simulates the fd
//     table logic and prints/counts
//   * SyscallExecutor — actually executes syscalls in the sandbox
//     (openat/read/write/...)
//
// The executor internally holds an FdTable and a PathResolver, mapping the
// original operation onto this process with fd as authoritative. execute()
// returns the outcome for that event (success/failure + reason); the engine
// decides whether to continue.
// ============================================================================

/// Outcome of executing a single event
struct ExecOutcome {
    bool   ok {true};
    bool   skipped {false};   // skipped due to filtering / unknown op, etc. (not an error)
    std::string note;         // diagnostic info (e.g. failure reason, skip reason)
};

class IExecutor {
public:
    virtual ~IExecutor() = default;

    /**
     * @brief Execute a single event
     *
     * The implementation must handle:
     *   * openat/open → resolve path, really open, register in the fd table
     *   * read/write/... → look up ourFd in the fd table, execute on this
     *     process's ourFd
     *   * close → look up the fd table, really close, unregister
     *   * stat/rename/mkdir/... → resolve path, really execute
     *   * any other unknown syscall → skipped
     */
    [[nodiscard]] virtual Result<ExecOutcome> execute(const TraceEvent& ev) = 0;

    /// The executor's internal fd table (used by the engine for diagnostics/stats)
    [[nodiscard]] virtual const FdTable& fdTable() const noexcept = 0;
};

}  // namespace trace_replay
