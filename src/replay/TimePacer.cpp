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

    // Original gap between adjacent events (seconds). The trace may have tiny
    // negative values at bucket boundaries (residual near-sorted inversions
    // from rawproc); clamp with max(0, ...).
    const double deltaSec = machineTs > m_lastTs ? (machineTs - m_lastTs) : 0.0;
    m_lastTs = machineTs;

    double sleepSec = deltaSec;
    if (m_mode == PaceMode::Scaled) {
        sleepSec = deltaSec / m_speed;
    }

    if (sleepSec > 0.0) {
        // Clamp to a sane upper bound to avoid sleeping too long in one call
        // (when a trace gap is abnormally large)
        if (sleepSec > 3600.0) {
            sleepSec = 3600.0;
        }
        std::this_thread::sleep_for(
            std::chrono::duration<double>{sleepSec});
    }
}

}  // namespace trace_replay
