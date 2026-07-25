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
// 事件归并器
//
// rawproc 的桶间全局有序、桶内 (machine_ts, log_offset) 有序。EventMerger
// 为每个桶创建一个 ParquetEventReader，再做 K 路小顶堆归并，对外吐出全局
// 时间序的事件流。这就是 replay 时间序列的来源。
//
// 归并键：先 machine_ts，再 log_offset（与 rawproc 排序键一致，保证稳定）。
// ============================================================================

/**
 * @brief 多桶事件归并器，产出全局时间序事件流
 */
class EventMerger {
public:
    EventMerger();
    ~EventMerger();

    EventMerger(const EventMerger&)            = delete;
    EventMerger& operator=(const EventMerger&) = delete;

    /**
     * @brief 打开 eventsRoot 下符合配置范围的全部桶
     *
     * 枚举 events_tsorted/bucket=NNNNNN，按 bucketMin/bucketMax/skipUnparsed
     * 过滤，为每个桶创建一个 reader 压入归并堆。
     */
    [[nodiscard]] Result<void> open(const ReplayConfig& cfg);

    /**
     * @brief 取下一条全局时间序事件
     *
     * @return std::nullopt 表示全部桶读完。
     * @warning 返回事件中的 string_view 字段在下次调用后失效（reader 缓冲会被
     *          下一行覆盖）。调用方若需跨调用持有，应自行拷贝。
     */
    [[nodiscard]] Result<std::optional<TraceEvent>> next();

    /// 已打开的桶数量
    [[nodiscard]] size_t readerCount() const noexcept { return m_readers.size(); }

private:
    /// 归并堆节点：当前 reader 缓存的头部事件 + reader 索引
    struct HeapNode {
        TraceEvent event;
        size_t     readerIdx {0};

        /// 小顶堆：machine_ts 小者优先；相等时 log_offset 小者优先
        bool operator>(const HeapNode& other) const
        {
            if (event.machineTs != other.event.machineTs) {
                return event.machineTs > other.event.machineTs;
            }
            return event.logOffset > other.event.logOffset;
        }
    };

    /// 首次预取每个 reader 的头部，建立初始堆
    [[nodiscard]] Result<void> primeHeap();

    std::vector<std::unique_ptr<IEventReader>> m_readers;
    std::vector<TraceEvent>                    m_heads;   // 每个 reader 当前头部事件（按 reader 顺序）
    std::vector<bool>                          m_done;    // 每个 reader 是否已读完
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> m_heap;
    bool m_opened {false};
};

}  // namespace trace_replay
