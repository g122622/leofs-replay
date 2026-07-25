#pragma once

#include "trace_replay/io/IEventReader.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace trace_replay {

// ============================================================================
// Parquet 事件读取器
//
// 读取单个 rawproc 桶目录（events_tsorted/bucket=NNNNNN）下的 Parquet 文件，
// 按 Arrow RecordBatch 流式遍历，逐行构造 TraceEvent。由于 rawproc 已在桶内
// 按 (machine_ts, log_offset) 排序，本读取器只需顺序吐出即可，无需再排序。
//
// 列映射对齐 rawproc 的 EVENT_COLS（见 context.md）。string_view 字段指向
// 内部行缓冲，next() 后失效。
// ============================================================================

/**
 * @brief 单桶 Parquet 流式读取器
 */
class ParquetEventReader final : public IEventReader {
public:
    /**
     * @param bucketDir  桶目录路径，如 .../events_tsorted/bucket=008467
     * @param bucket     桶编号（仅诊断用）
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
    // pImpl 隔离 Arrow 头文件，避免污染公共 include
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    long m_bucket;
};

}  // namespace trace_replay
