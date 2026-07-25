#pragma once

#include "trace_replay/config/ReplayConfig.hpp"

#include <chrono>

namespace trace_replay {

// ============================================================================
// 回放节拍器
//
// 按配置的 PaceMode 在事件之间插入等待，控制回放时间分布：
//   * Fast    —— 不等待，立即返回
//   * Real    —— sleep 原始间隔（machineTs 之差），1:1 还原
//   * Scaled  —— sleep 间隔 / speed
//
// 注意：machineTs 是机器启动后的秒数（raw ts），事件间隔即相邻事件 ts 之差。
// 首个事件以"无等待"开始。
// ============================================================================

class TimePacer {
public:
    explicit TimePacer(const ReplayConfig& cfg);

    /**
     * @brief 在处理下一条事件前按节拍等待
     *
     * @param machineTs 下一条事件的机器时间戳
     */
    void pace(double machineTs);

private:
    PaceMode m_mode;
    double   m_speed;
    double   m_lastTs {-1.0};   // <0 表示尚未开始
    bool     m_first {true};
};

}  // namespace trace_replay
