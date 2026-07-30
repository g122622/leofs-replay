#pragma once

#include "trace_replay/config/ReplayConfig.hpp"
#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/io/IEventReader.hpp"

#include <memory>
#include <optional>
#include <queue>
#include <vector>

namespace trace_replay {

// ============================================================================
// Event merger
//
// rawproc's buckets are globally ordered, and within a bucket ordered by
// (machine_ts, log_offset). EventMerger creates one ParquetEventReader per
// bucket and then performs a K-way min-heap merge, emitting a globally
// time-ordered event stream. This is the source of the replay time series.
//
// Merge key: first machine_ts, then log_offset (identical to rawproc's sort
// key, ensuring stability).
// ============================================================================

/**
 * @brief Multi-bucket event merger that produces a globally time-ordered stream
 */
class EventMerger {
public:
    EventMerger();
    ~EventMerger();

    EventMerger(const EventMerger&)            = delete;
    EventMerger& operator=(const EventMerger&) = delete;

    /**
     * @brief Open all buckets under eventsRoot that fall within the configured range
     *
     * Enumerate events_tsorted/bucket=NNNNNN, filter by bucketMin/bucketMax/
     * skipUnparsed, and create a reader for each bucket, pushing it into the
     * merge heap.
     */
    [[nodiscard]] Result<void> open(const ReplayConfig& cfg);

    /**
     * @brief Fetch the next globally time-ordered event
     *
     * @return std::nullopt means all buckets are exhausted.
     * @warning The string_view fields of the returned event are invalidated
     *          after the next call (the reader buffer is overwritten by the
     *          next row). Copy if you need to hold it across calls.
     */
    [[nodiscard]] Result<std::optional<TraceEvent>> next();

    /// Number of buckets opened
    [[nodiscard]] size_t readerCount() const noexcept { return m_readers.size(); }

private:
    /// Merge-heap node: the current reader's cached head event + reader index
    struct HeapNode {
        TraceEvent event;
        size_t     readerIdx {0};

        /// Min-heap: smaller machine_ts first; on tie, smaller log_offset first
        bool operator>(const HeapNode& other) const
        {
            if (event.machineTs != other.event.machineTs) {
                return event.machineTs > other.event.machineTs;
            }
            return event.logOffset > other.event.logOffset;
        }
    };

    /// Prefetch each reader's head to build the initial heap
    [[nodiscard]] Result<void> primeHeap();

    std::vector<std::unique_ptr<IEventReader>> m_readers;
    std::vector<TraceEvent>                    m_heads;   // each reader's current head event (in reader order)
    std::vector<bool>                          m_done;    // whether each reader is exhausted
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> m_heap;
    bool m_opened {false};
};

}  // namespace trace_replay
