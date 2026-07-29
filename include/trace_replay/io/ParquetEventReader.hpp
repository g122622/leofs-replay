#pragma once

#include "trace_replay/io/IEventReader.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace trace_replay {

// ============================================================================
// Parquet 事件读取器
//
// 两种输入形态：
//   1) 单个 .parquet 文件 —— 直读模式。用于用户提供的扁平 part-xxxx.parquet
//      （已全局按 (machine_ts, log_offset) 排序，无需分桶），此时 EventMerger
//      只挂一个读取器，K 路堆退化为直通。
//   2) 桶目录（events_tsorted/bucket=NNNNNN）—— rawproc 标准产出。桶内可能
//      含多个 part 文件，合并后顺序吐出；rawproc 已在桶内排序，本读取器只需
//      顺序吐出即可，无需再排序。
//
// 列映射对齐 rawproc 的 EVENT_COLS（见 context.md）。string_view 字段指向
// 内部行缓冲，next() 后失效。
// ============================================================================

/**
 * @brief 单桶/单文件 Parquet 流式读取器
 */
class ParquetEventReader final : public IEventReader {
public:
    /**
     * @param bucketDir  桶目录路径，或单个 .parquet 文件路径
     * @param bucket     桶编号（仅诊断用；单文件直读时可传 0）
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
