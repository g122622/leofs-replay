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

/// Parse a bucket directory name "bucket=008467" → number 8467; returns nullopt
/// if invalid.
std::optional<long> parseBucketName(const std::string& dirName)
{
    constexpr std::string_view prefix = "bucket=";
    if (dirName.size() <= prefix.size() || dirName.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    std::string num = dirName.substr(prefix.size());
    // Parse after skipping leading zeros
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

/// Range check: whether the bucket number falls within the configured range
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

    // —— Single-file direct-read mode ——
    // When events_root points directly at a .parquet file, bypass the bucketed
    // directory enumeration and attach only one reader. The file is already
    // globally sorted; the K-way heap degenerates to a passthrough.
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
            "events_tsorted directory not found: " + tsorted.string() +
            " (events_root may also point directly at a single .parquet file)",
            "EventMerger::open");
    }

    // Enumerate all bucket= directories, sort by number, then build a reader
    // for each.
    std::vector<std::pair<long, fs::path>> buckets;
    for (auto& entry : fs::directory_iterator{tsorted, ec}) {
        if (!entry.is_directory()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        auto bucket = parseBucketName(name);
        if (!bucket) {
            // Special buckets like _unparsed / null_ts: skip per config
            if (cfg.skipUnparsed) {
                continue;
            }
            // When not skipping, they still cannot be merged into the numeric
            // order; ignore them (only numeric buckets take part in the global
            // time order)
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
    // Prefetch each reader's first event and push it into the heap. Mark empty
    // readers as done.
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

    // Pop the global minimum and refill that reader's next event
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
