#include "trace_replay/io/ParquetEventReader.hpp"

#include "trace_replay/core/Error.hpp"
#include "trace_replay/core/SyscallClassify.hpp"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace trace_replay {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Column-access helpers: Arrow's ChunkedArray is composed of multiple chunks;
// fetch row `row` across chunks. All access goes through const pointers
// (chunks() returns a const array) to avoid losing constness.
// ---------------------------------------------------------------------------

/// Return a view of row `row` in a string column, pointing into Arrow's
/// internal buffer (stable for the lifetime of the Table).
std::string_view sliceString(const std::shared_ptr<arrow::ChunkedArray>& col, i64 row)
{
    if (!col) {
        return {};
    }
    i64 remaining = row;
    for (const auto& chunk : col->chunks()) {
        const i64 len = chunk->length();
        if (remaining < len) {
            const auto& arr = chunk;   // shared_ptr<arrow::Array>
            if (auto s = std::dynamic_pointer_cast<arrow::StringArray>(arr)) {
                return std::string_view{s->GetView(static_cast<int>(remaining))};
            }
            if (auto b = std::dynamic_pointer_cast<arrow::BinaryArray>(arr)) {
                return std::string_view{b->GetView(static_cast<int>(remaining))};
            }
            if (auto ls = std::dynamic_pointer_cast<arrow::LargeStringArray>(arr)) {
                return std::string_view{ls->GetView(remaining)};
            }
            return {};
        }
        remaining -= len;
    }
    return {};
}

/// Return row `row` of an integer column (int64/int32). null is treated as 0.
i64 getInt64(const std::shared_ptr<arrow::ChunkedArray>& col, i64 row)
{
    if (!col) {
        return 0;
    }
    i64 remaining = row;
    for (const auto& chunk : col->chunks()) {
        const i64 len = chunk->length();
        if (remaining < len) {
            if (auto a = std::dynamic_pointer_cast<arrow::Int64Array>(chunk)) {
                return a->IsNull(static_cast<int>(remaining)) ? 0
                                                              : a->Value(static_cast<int>(remaining));
            }
            if (auto a = std::dynamic_pointer_cast<arrow::Int32Array>(chunk)) {
                return a->IsNull(static_cast<int>(remaining)) ? 0
                                                              : a->Value(static_cast<int>(remaining));
            }
            return 0;
        }
        remaining -= len;
    }
    return 0;
}

/// Return row `row` of a float column (machine_ts). null is treated as 0.
double getDouble(const std::shared_ptr<arrow::ChunkedArray>& col, i64 row)
{
    if (!col) {
        return 0.0;
    }
    i64 remaining = row;
    for (const auto& chunk : col->chunks()) {
        const i64 len = chunk->length();
        if (remaining < len) {
            if (auto a = std::dynamic_pointer_cast<arrow::DoubleArray>(chunk)) {
                return a->IsNull(static_cast<int>(remaining)) ? 0.0
                                                              : a->Value(static_cast<int>(remaining));
            }
            if (auto a = std::dynamic_pointer_cast<arrow::FloatArray>(chunk)) {
                // Explicit cast to double to avoid -Wdouble-promotion
                // (float→double precision widening)
                return a->IsNull(static_cast<int>(remaining))
                           ? 0.0
                           : static_cast<double>(a->Value(static_cast<int>(remaining)));
            }
            return 0.0;
        }
        remaining -= len;
    }
    return 0.0;
}

/// Parse a numeric argument string from the original trace.
/// rawproc stores ret/err/arg1/arg2 as [decimal] strings (e.g. openat's flags
/// "524288"=0x80000=O_CLOEXEC, read's byte count "32768"). Only when the "0x"
/// prefix is present is it parsed as hexadecimal, for compatibility with the
/// rare original line.
i64 parseNum(std::string_view s)
{
    if (s.empty()) {
        return 0;
    }
    bool hex = false;
    if (s.starts_with("0x") || s.starts_with("0X")) {
        s.remove_prefix(2);
        hex = true;
    }
    i64 value = 0;
    for (char c : s) {
        int digit = 0;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (hex && c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (hex && c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        } else {
            break;
        }
        value = value * (hex ? 16 : 10) + digit;
    }
    return value;
}

Side parseSide(std::string_view s)
{
    if (s == "source") return Side::Source;
    if (s == "target") return Side::Target;
    return Side::Other;
}

/// Look up a column index by name in the schema; returns -1 if missing.
int findColumn(const std::shared_ptr<arrow::Schema>& schema, std::string_view name)
{
    for (int i = 0; i < schema->num_fields(); ++i) {
        if (schema->field(i)->name() == name) {
            return i;
        }
    }
    return -1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl: encapsulates the Arrow/Parquet read state, isolating header deps.
// ---------------------------------------------------------------------------
struct ParquetEventReader::Impl {
    std::shared_ptr<arrow::Table>           table;
    std::shared_ptr<arrow::Schema>          schema;
    i64                                     numRows {0};
    i64                                     curRow  {0};

    // Cache frequently used columns to avoid per-name lookups each time
    std::shared_ptr<arrow::ChunkedArray> machineTs;
    std::shared_ptr<arrow::ChunkedArray> logOffset;
    std::shared_ptr<arrow::ChunkedArray> pid;
    std::shared_ptr<arrow::ChunkedArray> comm;
    std::shared_ptr<arrow::ChunkedArray> sc;
    std::shared_ptr<arrow::ChunkedArray> ret;
    std::shared_ptr<arrow::ChunkedArray> err;
    std::shared_ptr<arrow::ChunkedArray> arg1;
    std::shared_ptr<arrow::ChunkedArray> arg2;
    std::shared_ptr<arrow::ChunkedArray> path;
    std::shared_ptr<arrow::ChunkedArray> renameSrc;
    std::shared_ptr<arrow::ChunkedArray> renameDst;
    std::shared_ptr<arrow::ChunkedArray> canonicalPath;
    std::shared_ptr<arrow::ChunkedArray> mapped;
    std::shared_ptr<arrow::ChunkedArray> side;
};

ParquetEventReader::ParquetEventReader(fs::path bucketDir, long bucket)
    : m_impl(std::make_unique<Impl>())
    , m_bucket(bucket)
{
    // Collect the .parquet files to read. Two input forms are supported:
    //   1) bucketDir is a single .parquet file — single-file direct-read mode
    //      (a user-supplied flat part-xxxx.parquet, already globally sorted, no
    //      bucketing needed);
    //   2) bucketDir is a directory — a rawproc bucket directory; enumerate all
    //      .parquet part files under it.
    // Paths are stored into partFiles as UTF-8: Arrow's std::string path API
    // interprets bytes as UTF-8 on all platforms; on Windows, using
    // path::string() (ACP-encoded) would corrupt paths containing CJK.
    auto toUtf8 = [](const fs::path& p) -> std::string {
        auto u8 = p.u8string();   // std::u8string, UTF-8 encoded
        return std::string{reinterpret_cast<const char*>(u8.data()), u8.size()};
    };
    std::vector<std::string> partFiles;
    std::error_code ec;
    if (fs::is_regular_file(bucketDir, ec) &&
        bucketDir.extension() == ".parquet") {
        partFiles.push_back(toUtf8(bucketDir));
    } else {
        for (auto& entry : fs::directory_iterator{bucketDir, ec}) {
            if (entry.is_regular_file() && entry.path().extension() == ".parquet") {
                partFiles.push_back(toUtf8(entry.path()));
            }
        }
    }

    // Read each file into a Table via parquet::arrow::OpenFile + ReadTable,
    // then merge.
    std::vector<std::shared_ptr<arrow::Table>> tables;
    tables.reserve(partFiles.size());
    for (const auto& f : partFiles) {
        auto opened = arrow::io::ReadableFile::Open(f);
        if (!opened.ok()) {
            continue;   // record the open failure but do not abort; numRows is
                        // checked later
        }
        // Arrow 24: parquet::arrow::OpenFile returns
        // Result<unique_ptr<FileReader>>
        auto readerResult = parquet::arrow::OpenFile(opened.ValueOrDie(),
                                                     arrow::default_memory_pool());
        if (!readerResult.ok()) {
            continue;
        }
        auto& reader = *readerResult;
        // ReadTable() returns arrow::Result<shared_ptr<Table>>
        auto tableResult = reader->ReadTable();
        if (tableResult.ok() && *tableResult) {
            tables.push_back(*tableResult);
        }
    }

    if (!tables.empty()) {
        if (tables.size() == 1) {
            m_impl->table = tables.front();
        } else {
            auto combined = arrow::ConcatenateTables(tables);
            if (combined.ok()) {
                m_impl->table = combined.ValueOrDie();
            }
        }
    }

    if (m_impl->table) {
        m_impl->schema  = m_impl->table->schema();
        m_impl->numRows = m_impl->table->num_rows();

        // Column names aligned with rawproc EVENT_COLS
        auto bind = [&](std::shared_ptr<arrow::ChunkedArray>& dst, std::string_view name) {
            int idx = findColumn(m_impl->schema, name);
            if (idx >= 0) {
                dst = m_impl->table->column(idx);
            }
        };
        bind(m_impl->machineTs,     "machine_ts");
        bind(m_impl->logOffset,     "log_offset");
        bind(m_impl->pid,           "pid");
        bind(m_impl->comm,          "comm");
        bind(m_impl->sc,            "sc");
        bind(m_impl->ret,           "ret");
        bind(m_impl->err,           "err");
        bind(m_impl->arg1,          "arg1");
        bind(m_impl->arg2,          "arg2");
        bind(m_impl->path,          "path");
        bind(m_impl->renameSrc,     "rename_src");
        bind(m_impl->renameDst,     "rename_dst");
        bind(m_impl->canonicalPath, "canonical_path");
        bind(m_impl->mapped,        "mapped");
        bind(m_impl->side,          "side");
    }
}

ParquetEventReader::~ParquetEventReader() = default;
ParquetEventReader::ParquetEventReader(ParquetEventReader&&) noexcept = default;
ParquetEventReader& ParquetEventReader::operator=(ParquetEventReader&&) noexcept = default;

Result<std::optional<TraceEvent>> ParquetEventReader::next()
{
    if (!m_impl->table || m_impl->curRow >= m_impl->numRows) {
        return std::optional<TraceEvent>{};
    }

    const i64 row = m_impl->curRow++;
    auto& c = m_impl;

    TraceEvent ev;
    ev.bucket     = m_bucket;
    ev.machineTs  = getDouble(c->machineTs, row);
    ev.logOffset  = getInt64(c->logOffset, row);
    ev.pid        = getInt64(c->pid, row);

    // ret/err/arg1/arg2 are [string columns] (decimal) in rawproc's output;
    // parsed here.
    //   openat: ret=fd (small int), arg1=flags (e.g. 524288=O_CLOEXEC)
    //   read/write: ret=actual byte count, arg1=requested byte count (count),
    //               fd is not here
    //   close: ret=0, fd is not here
    ev.ret        = parseNum(sliceString(c->ret, row));
    ev.err        = parseNum(sliceString(c->err, row));
    ev.arg1Num    = parseNum(sliceString(c->arg1, row));
    ev.arg2Num    = parseNum(sliceString(c->arg2, row));

    ev.comm          = std::string{sliceString(c->comm, row)};
    ev.sc            = std::string{sliceString(c->sc, row)};
    ev.path          = std::string{sliceString(c->path, row)};
    ev.renameSrc     = std::string{sliceString(c->renameSrc, row)};
    ev.renameDst     = std::string{sliceString(c->renameDst, row)};
    ev.canonicalPath = std::string{sliceString(c->canonicalPath, row)};
    // Do not trust rawproc's op_class column; re-classify by sc to keep this
    // tool self-consistent
    ev.opClass = classifyOp(ev.sc);

    if (c->mapped) {
        ev.mapped = getInt64(c->mapped, row) != 0;
    }
    if (c->side) {
        ev.side = parseSide(sliceString(c->side, row));
    }

    return std::optional<TraceEvent>{std::move(ev)};
}

}  // namespace trace_replay
