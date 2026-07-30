#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"

#include <optional>

namespace trace_replay {

// ============================================================================
// Event reader interface
//
// rawproc's output is bucketed: events_tsorted/bucket=NNNNNN, globally ordered
// across buckets and ordered by (machine_ts, log_offset) within a bucket. One
// reader is responsible for streaming out already-sorted events "in bucket
// order". Readers for multiple buckets are then merged by EventMerger to obtain
// the global time order.
//
// The string_view fields of the TraceEvent returned by a reader point into the
// reader's internal buffer and are valid only until the next call to next()
// (iterator-invalidation semantics; the caller must consume promptly).
// ============================================================================

class IEventReader {
public:
    virtual ~IEventReader() = default;

    /**
     * @brief Fetch the next event
     *
     * @return std::nullopt means this reader (bucket) is exhausted; otherwise
     *         returns the event, whose string_view fields become invalid after
     *         the next next() call.
     */
    [[nodiscard]] virtual Result<std::optional<TraceEvent>> next() = 0;

    /// The bucket number this reader corresponds to (diagnostic)
    [[nodiscard]] virtual long bucket() const noexcept = 0;
};

}  // namespace trace_replay
