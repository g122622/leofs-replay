#pragma once

#include "trace_replay/config/ReplayConfig.hpp"
#include "trace_replay/core/Result.hpp"
#include "trace_replay/io/EventMerger.hpp"
#include "trace_replay/replay/IExecutor.hpp"
#include "trace_replay/replay/TimePacer.hpp"

#include <memory>
#include <ostream>

namespace trace_replay {

// ============================================================================
// Replay engine
//
// Orchestrates: EventMerger (globally time-ordered event stream) → TimePacer
// (pacing) → IExecutor (execution). Handles event filtering (pid/side), error
// policy (continueOnError), and run statistics.
// ============================================================================

/// Run statistics
struct ReplayStats {
    u64 totalEvents {0};     // total events emitted by the merger
    u64 processed {0};       // events actually executed (including skipped)
    u64 skipped {0};         // events skipped
    u64 failed {0};          // events that failed to execute
    u64 filtered {0};        // events removed by the filter
    double firstTs {0.0};
    double lastTs {0.0};
};

/**
 * @brief Replay engine
 */
class ReplayEngine {
public:
    ReplayEngine(ReplayConfig cfg,
                 std::unique_ptr<IExecutor> executor,
                 std::ostream& log);

    /**
     * @brief Run the full replay
     *
     * Open all buckets, traverse events in time order, pace, execute, and
     * finally output statistics.
     */
    [[nodiscard]] Result<ReplayStats> run();

private:
    /// Whether an event passes the filter (pid / side)
    [[nodiscard]] bool passesFilter(const TraceEvent& ev) const noexcept;

    ReplayConfig                  m_cfg;
    EventMerger                   m_merger;
    std::unique_ptr<IExecutor>    m_executor;
    TimePacer                     m_pacer;
    std::ostream&                 m_log;
};

}  // namespace trace_replay
