#include "trace_replay/io/EventMerger.hpp"

#include "trace_replay/core/Assert.hpp"
#include "trace_replay/core/Error.hpp"
#include "trace_replay/io/ParquetEventReader.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace trace_replay {
namespace {

namespace fs = std::filesystem;

/// 解析桶目录名 "bucket=008467" → 数字 8467；非法返回 nullopt
std::optional<long> parseBucketName(const std::string& dirName)
{
    constexpr std::string_view prefix = "bucket=";
    if (dirName.size() <= prefix.size() || dirName.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    std::string num = dirName.substr(prefix.size());
    // 跳过前导零后解析
    try {
        size_t pos = 0;
        long v = std::stol(num, &pos);
        if (pos != num.size()) {
            return std::nullopt;
        }
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

/// 窗口判断：桶编号是否落在配置范围内
bool inRange(long bucket, const ReplayConfig& cfg)
{
    if (bucket < cfg.bucketMin) {
        return false;
    }
    return cfg.bucketMax < 0 || bucket <= cfg.bucketMax;
}

}  // namespace

EventMerger::EventMerger() = default;
EventMerger::~EventMerger() = default;

Result<void> EventMerger::open(const ReplayConfig& cfg)
{
    TR_ASSERT(!m_opened);

    std::error_code ec;

    // —— 单文件直读模式 ——
    // 当 events_root 直接指向一个 .parquet 文件时，绕过分桶目录枚举，
    // 只挂一个读取器。文件已全局排序，K 路堆退化为直通。
    if (fs::is_regular_file(cfg.eventsRoot, ec) &&
        cfg.eventsRoot.extension() == ".parquet") {
        m_readers.reserve(1);
        m_heads.resize(1);
        m_done.assign(1, false);
        m_readers.push_back(std::make_unique<ParquetEventReader>(cfg.eventsRoot, 0));
        m_opened = true;
        TR_TRY_VOID(primeHeap());
        return Result<void>::ok();
    }

    const fs::path tsorted = cfg.eventsRoot / "events_tsorted";
    if (!fs::exists(tsorted, ec)) {
        return Error::notFound(
            "未找到 events_tsorted 目录: " + tsorted.string() +
            "（events_root 也可直接指向单个 .parquet 文件）",
            "EventMerger::open");
    }

    // 枚举所有 bucket= 目录，按编号排序后建 reader
    std::vector<std::pair<long, fs::path>> buckets;
    for (auto& entry : fs::directory_iterator{tsorted, ec}) {
        if (!entry.is_directory()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        auto bucket = parseBucketName(name);
        if (!bucket) {
            // _unparsed / null_ts 等特殊桶：按配置决定是否跳过
            if (cfg.skipUnparsed) {
                continue;
            }
            // 不跳过时无法归并入数字序，仍忽略（仅数字桶参与全局时间序）
            continue;
        }
        if (!inRange(*bucket, cfg)) {
            continue;
        }
        buckets.emplace_back(*bucket, entry.path());
    }

    std::sort(buckets.begin(), buckets.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    m_readers.reserve(buckets.size());
    m_heads.resize(buckets.size());
    m_done.assign(buckets.size(), false);

    for (auto& [bucket, dir] : buckets) {
        m_readers.push_back(std::make_unique<ParquetEventReader>(dir, bucket));
    }

    m_opened = true;

    TR_TRY_VOID(primeHeap());
    return Result<void>::ok();
}

Result<void> EventMerger::primeHeap()
{
    // 预取每个 reader 的首个事件，压入堆。空 reader 标记 done。
    for (size_t i = 0; i < m_readers.size(); ++i) {
        TR_TRY(opt, m_readers[i]->next());
        if (!opt) {
            m_done[i] = true;
            continue;
        }
        m_heads[i] = std::move(*opt);
        m_heap.push(HeapNode{m_heads[i], i});
    }
    return Result<void>::ok();
}

Result<std::optional<TraceEvent>> EventMerger::next()
{
    if (m_heap.empty()) {
        return std::optional<TraceEvent>{};
    }

    // 弹出全局最小，回填该 reader 的下一条
    HeapNode top = m_heap.top();
    m_heap.pop();

    const size_t idx = top.readerIdx;
    TraceEvent out = std::move(top.event);

    if (!m_done[idx]) {
        TR_TRY(opt, m_readers[idx]->next());
        if (!opt) {
            m_done[idx] = true;
        } else {
            m_heads[idx] = std::move(*opt);
            m_heap.push(HeapNode{m_heads[idx], idx});
        }
    }

    return std::optional<TraceEvent>{std::move(out)};
}

}  // namespace trace_replay
