#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/core/Types.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trace_replay {

// ============================================================================
// Replay pacing mode
//
//   Fast      — no sleep; process in time order as fast as possible (default;
//               focuses on correctness and throughput)
//   Real      — sleep the original wall-clock gap between events; reproduce the
//               real-time distribution 1:1
//   Scaled    — scale the gap by `speed` (speed>1 faster, <1 slower)
// ============================================================================

enum class PaceMode : u8 {
    Fast,
    Real,
    Scaled,
};

/**
 * @brief replay run configuration
 *
 * Constructed from a JSON config file (see ReplayConfig::loadFromJson). All
 * paths are resolved relative to the sandbox root sandboxRoot and checked for
 * path traversal. Field names align with the JSON keys (snake_case).
 */
struct ReplayConfig {
    // —— Input ——
    /// rawproc output root directory; should contain events_tsorted/bucket=xxxxxx
    std::filesystem::path eventsRoot;
    /// Bucket directory name width (default 6, aligned with rawproc width=max(6, ...))
    int bucketWidth {6};
    /// Only replay buckets within the specified range (closed interval); empty means all
    long bucketMin {0};
    long bucketMax {-1};   // <0 means no upper bound

    // —— Sandbox ——
    /// Root directory for real syscalls; all replay paths are confined under it
    std::filesystem::path sandboxRoot;

    // —— Pacing ——
    PaceMode paceMode {PaceMode::Fast};
    /// Speed multiplier in Scaled mode (2.0 = two-times-speed replay)
    double speed {1.0};

    // —— Filtering ——
    /// Only replay events of these pids (empty means all)
    std::vector<i64> pidFilter;
    /// Only replay the source side / target side / all
    Side sideFilter {Side::Other};   // Other means no filtering
    /// Whether to skip _unparsed / null_ts buckets
    bool skipUnparsed {true};

    // —— Behavior ——
    /// Whether to only do a dry-run (no real syscalls, just print/count)
    bool dryRun {false};
    /// Max bytes per read/write (avoids reading an oversized trace offset at once)
    i64 maxIoBytes {1 << 20};   // default 1 MiB
    /// Whether to continue on syscall failure (true = skip and count, false = abort)
    bool continueOnError {true};
    /// Replay event cap (>0 stops after that many; useful for a bounded dry-run on a
    /// large file; 0 = unlimited)
    u64 maxEvents {0};

    /**
     * @brief Load from a JSON config file
     *
     * See README for an example of the expected JSON structure. Missing fields
     * use the struct defaults.
     *
     * @param path JSON config file path
     * @return Result<ReplayConfig>
     */
    [[nodiscard]] static Result<ReplayConfig> loadFromJson(std::string_view path);

    /**
     * @brief Validate the config and normalize paths
     *
     * Converts relative paths to absolute, checks that sandboxRoot exists,
     * eventsRoot exists, etc.
     */
    [[nodiscard]] Result<void> validate();
};

}  // namespace trace_replay
