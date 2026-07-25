#include "trace_replay/replay/TimePacer.hpp"

#include "trace_replay/core/Assert.hpp"

#include <chrono>
#include <thread>

namespace trace_replay {

TimePacer::TimePacer(const ReplayConfig& cfg)
    : m_mode(cfg.paceMode)
    , m_speed(cfg.speed)
{
    TR_ASSERT(m_mode != PaceMode::Scaled || m_speed > 0.0);
}

void TimePacer::pace(double machineTs)
{
    if (m_mode == PaceMode::Fast) {
        m_lastTs = machineTs;
        return;
    }

    if (m_first) {
        m_first = false;
        m_lastTs = machineTs;
        return;
    }

    // 相邻事件原始间隔（秒）。trace 中可能因桶边界出现极小负值（rawproc 的
    // near-sorted 残余倒置），用 max(0,...) 兜住。
    const double deltaSec = machineTs > m_lastTs ? (machineTs - m_lastTs) : 0.0;
    m_lastTs = machineTs;

    double sleepSec = deltaSec;
    if (m_mode == PaceMode::Scaled) {
        sleepSec = deltaSec / m_speed;
    }

    if (sleepSec > 0.0) {
        // 截断到合理上限，避免单次 sleep 过久（trace 间隔异常大时）
        if (sleepSec > 3600.0) {
            sleepSec = 3600.0;
        }
        std::this_thread::sleep_for(
            std::chrono::duration<double>{sleepSec});
    }
}

}  // namespace trace_replay
