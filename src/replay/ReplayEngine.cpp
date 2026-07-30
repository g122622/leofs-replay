#include "trace_replay/replay/ReplayEngine.hpp"

#include "trace_replay/core/Error.hpp"

#include <algorithm>
#include <format>
#include <ostream>

namespace trace_replay {

ReplayEngine::ReplayEngine(ReplayConfig cfg,
                           std::unique_ptr<IExecutor> executor,
                           std::ostream& log)
    : m_cfg(std::move(cfg))
    , m_executor(std::move(executor))
    , m_pacer(m_cfg)
    , m_log(log) {}

bool ReplayEngine::passesFilter(const TraceEvent& ev) const noexcept
{
    // pid filter
    if (!m_cfg.pidFilter.empty()) {
        if (std::find(m_cfg.pidFilter.begin(), m_cfg.pidFilter.end(), ev.pid)
            == m_cfg.pidFilter.end()) {
            return false;
        }
    }
    // side filter (Other means no filtering)
    if (m_cfg.sideFilter != Side::Other && ev.side != m_cfg.sideFilter) {
        return false;
    }
    return true;
}

Result<ReplayStats> ReplayEngine::run()
{
    TR_TRY_VOID(m_merger.open(m_cfg));
    m_log << std::format("[engine] opened {} buckets\n", m_merger.readerCount());

    ReplayStats stats;
    bool first = true;

    while (true) {
        TR_TRY(opt, m_merger.next());
        if (!opt) {
            break;
        }
        TraceEvent ev = std::move(*opt);
        ++stats.totalEvents;

        if (first) {
            stats.firstTs = ev.machineTs;
            first = false;
        }
        stats.lastTs = ev.machineTs;

        if (!passesFilter(ev)) {
            ++stats.filtered;
            continue;
        }

        // Event cap (bounded dry-run): stop once reached; do not process further
        // events.
        if (m_cfg.maxEvents > 0 && stats.processed + stats.failed >= m_cfg.maxEvents) {
            m_log << std::format("[engine] reached max_events cap {}, stopping replay\n",
                                 m_cfg.maxEvents);
            break;
        }

        // Pacing: insert a wait according to the original time distribution
        m_pacer.pace(ev.machineTs);

        // Execute (may succeed / be skipped / fail)
        auto execRes = m_executor->execute(ev);
        if (!execRes.success()) {
            ++stats.failed;
            m_log << std::format("[engine] event execution failed @ts={:.6f} pid={} {}: {}\n",
                                 ev.machineTs, ev.pid, ev.sc,
                                 execRes.error().toString());
            if (!m_cfg.continueOnError) {
                return std::move(execRes).error();
            }
            continue;
        }
        ++stats.processed;
        if (execRes.value().skipped) {
            ++stats.skipped;
        }
    }

    // processed is accumulated in the loop (the count of events that passed the
    // filter and did not throw on execution);
    // skipped is the subset of those the executor judged as skipped.
    m_log << std::format(
        "[engine] replay done: total={} processed={} skipped={} failed={} filtered={} "
        "ts range=[{:.6f}, {:.6f}] fd table residual={}\n",
        stats.totalEvents, stats.processed, stats.skipped, stats.failed,
        stats.filtered, stats.firstTs, stats.lastTs, m_executor->fdTable().size());

    return stats;
}

}  // namespace trace_replay
