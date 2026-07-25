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
    // pid 过滤
    if (!m_cfg.pidFilter.empty()) {
        if (std::find(m_cfg.pidFilter.begin(), m_cfg.pidFilter.end(), ev.pid)
            == m_cfg.pidFilter.end()) {
            return false;
        }
    }
    // side 过滤（Other 表示不过滤）
    if (m_cfg.sideFilter != Side::Other && ev.side != m_cfg.sideFilter) {
        return false;
    }
    return true;
}

Result<ReplayStats> ReplayEngine::run()
{
    TR_TRY_VOID(m_merger.open(m_cfg));
    m_log << std::format("[engine] 已打开 {} 个桶\n", m_merger.readerCount());

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

        // 节拍：按原始时间分布插入等待
        m_pacer.pace(ev.machineTs);

        // 执行（可能成功/skipped/失败）
        auto execRes = m_executor->execute(ev);
        if (!execRes.success()) {
            ++stats.failed;
            m_log << std::format("[engine] 事件执行失败 @ts={:.6f} pid={} {}: {}\n",
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

    // processed 在循环内累加（通过过滤且执行未抛错的次数）；
    // skipped 是其中被执行器判定为跳过的子集。
    m_log << std::format(
        "[engine] 回放完成: total={} processed={} skipped={} failed={} filtered={} "
        "ts范围=[{:.6f}, {:.6f}] fd表残留={}\n",
        stats.totalEvents, stats.processed, stats.skipped, stats.failed,
        stats.filtered, stats.firstTs, stats.lastTs, m_executor->fdTable().size());

    return stats;
}

}  // namespace trace_replay
