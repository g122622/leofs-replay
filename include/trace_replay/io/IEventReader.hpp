#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"

#include <optional>

namespace trace_replay {

// ============================================================================
// 事件读取器接口
//
// rawproc 的产出是分桶的：events_tsorted/bucket=NNNNNN，桶间全局有序、桶内
// 按 (machine_ts, log_offset) 有序。一个读取器负责"按桶顺序"流式吐出已排好
// 序的事件。多个桶的读取器再由 EventMerger 归并即可得到全局时间序。
//
// 读取器返回的 TraceEvent 中的 string_view 指向读取器内部缓冲，仅在下次
// 调用 next() 之前有效（迭代器失效语义，调用方需及时消费）。
// ============================================================================

class IEventReader {
public:
    virtual ~IEventReader() = default;

    /**
     * @brief 取下一条事件
     *
     * @return std::nullopt 表示该读取器（桶）已读完；否则返回事件引用，
     *         其 string_view 字段在下一次 next() 后失效。
     */
    [[nodiscard]] virtual Result<std::optional<TraceEvent>> next() = 0;

    /// 当前读取器对应的桶编号（诊断用）
    [[nodiscard]] virtual long bucket() const noexcept = 0;
};

}  // namespace trace_replay
