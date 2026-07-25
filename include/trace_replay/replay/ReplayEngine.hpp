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
// 回放引擎
//
// 编排：EventMerger（全局时间序事件流）→ TimePacer（节拍）→ IExecutor（执行）。
// 负责事件过滤（pid/side）、错误策略（continueOnError）、运行统计。
// ============================================================================

/// 运行统计
struct ReplayStats {
    u64 totalEvents {0};     // 归并器吐出的总事件数
    u64 processed {0};       // 实际执行（含 skipped）的事件数
    u64 skipped {0};         // 被跳过的事件数
    u64 failed {0};          // 执行失败的事件数
    u64 filtered {0};        // 被过滤器剔除的事件数
    double firstTs {0.0};
    double lastTs {0.0};
};

/**
 * @brief 回放引擎
 */
class ReplayEngine {
public:
    ReplayEngine(ReplayConfig cfg,
                 std::unique_ptr<IExecutor> executor,
                 std::ostream& log);

    /**
     * @brief 执行完整回放
     *
     * 打开所有桶、按时间序遍历事件、节拍、执行，最后输出统计。
     */
    [[nodiscard]] Result<ReplayStats> run();

private:
    /// 事件是否通过过滤器（pid / side）
    [[nodiscard]] bool passesFilter(const TraceEvent& ev) const noexcept;

    ReplayConfig                  m_cfg;
    EventMerger                   m_merger;
    std::unique_ptr<IExecutor>    m_executor;
    TimePacer                     m_pacer;
    std::ostream&                 m_log;
};

}  // namespace trace_replay
