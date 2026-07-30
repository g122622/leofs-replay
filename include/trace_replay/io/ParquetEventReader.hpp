#pragma once

#include "trace_replay/io/IEventReader.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace trace_replay {

// ============================================================================
// Parquet event reader
//
// Two input forms:
//   1) A single .parquet file — direct-read mode. For a user-supplied flat
//      part-xxxx.parquet (already globally sorted by (machine_ts, log_offset),
//      no bucketing needed); here EventMerger attaches only one reader and the
//      K-way heap degenerates to a passthrough.
//   2) A bucket directory (events_tsorted/bucket=NNNNNN) — standard rawproc
//      output. A bucket may contain multiple part files; they are merged and
//      emitted in order. rawproc already sorted within the bucket, so this
//      reader only needs to emit sequentially — no further sorting.
//
// Column mapping is aligned with rawproc's EVENT_COLS (see context.md).
// string_view fields point into the internal row buffer and are invalidated
// after next().
// ============================================================================

/**
 * @brief Per-bucket / per-file streaming Parquet reader
 */
class ParquetEventReader final : public IEventReader {
public:
    /**
     * @param bucketDir  bucket directory path, or a single .parquet file path
     * @param bucket     bucket number (diagnostic only; pass 0 for single-file
     *                   direct read)
     */
    ParquetEventReader(std::filesystem::path bucketDir, long bucket);
    ~ParquetEventReader() override;

    ParquetEventReader(const ParquetEventReader&)            = delete;
    ParquetEventReader& operator=(const ParquetEventReader&) = delete;
    ParquetEventReader(ParquetEventReader&&) noexcept;
    ParquetEventReader& operator=(ParquetEventReader&&) noexcept;

    [[nodiscard]] Result<std::optional<TraceEvent>> next() override;
    [[nodiscard]] long bucket() const noexcept override { return m_bucket; }

private:
    // pImpl to isolate Arrow headers and avoid polluting the public include
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    long m_bucket;
};

}  // namespace trace_replay
