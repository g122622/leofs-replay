#pragma once

#include "trace_replay/config/ReplayConfig.hpp"

#include <chrono>

namespace trace_replay {

// ============================================================================
// Replay pacer
//
// Inserts waits between events according to the configured PaceMode, controlling
// the replay time distribution:
//   * Fast    — no wait, return immediately
//   * Real    — sleep the original gap (the difference of machineTs), 1:1
//   * Scaled  — sleep gap / speed
//
// Note: machineTs is seconds since machine boot (raw ts); the event gap is the
// ts difference between adjacent events. The first event starts with no wait.
// ============================================================================

class TimePacer {
public:
    explicit TimePacer(const ReplayConfig& cfg);

    /**
     * @brief Wait according to the pacing mode before processing the next event
     *
     * @param machineTs the next event's machine timestamp
     */
    void pace(double machineTs);

private:
    PaceMode m_mode;
    double   m_speed;
    double   m_lastTs {-1.0};   // <0 means not yet started
    bool     m_first {true};
};

}  // namespace trace_replay
