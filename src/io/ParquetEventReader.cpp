#include "trace_replay/io/ParquetEventReader.hpp"

#include "trace_replay/core/Error.hpp"
#include "trace_replay/core/SyscallClassify.hpp"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace trace_replay {
namespace {

namespace fs = std::filesystem;

/// 把一个字符串列的当前行值拷出为 string_view，指向 reader 内部缓冲。
/// Arrow 的 binary/string 列以 offset+value 平铺存储，这里取第 row 行的切片。
std::string_view sliceString(const std::shared_ptr<arrow::ChunkedArray>& col, i64 row)
{
    // ChunkedArray 按 chunk 平铺，需定位到 row 所在 chunk。
    i64 remaining = row;
    for (const auto& chunk : col->chunks()) {
        const auto len = chunk->length();
        if (remaining < len) {
            const auto* arr = chunk.get();
            // 仅支持 string/binary/large_string，否则返回空串
            if (const auto* s = dynamic_cast<arrow::StringArray*>(arr)) {
                return std::string_view{s->GetView(static_cast<int>(remaining))};
            }
            if (const auto* b = dynamic_cast<arrow::BinaryArray*>(arr)) {
                return std::string_view{b->GetView(static_cast<int>(remaining))};
            }
            if (const auto* ls = dynamic_cast<arrow::LargeStringArray*>(arr)) {
                return std::string_view{ls->GetView(remaining)};
            }
            return {};
        }
        remaining -= len;
    }
    return {};
}

/// 取整数列当前行值。trace 中 pid/log_offset/ret/err 多为 int64。
i64 getInt64(const std::shared_ptr<arrow::ChunkedArray>& col, i64 row)
{
    i64 remaining = row;
    for (const auto& chunk : col->chunks()) {
        const auto len = chunk->length();
        if (remaining < len) {
            const auto* arr = chunk.get();
            if (const auto* a = dynamic_cast<arrow::Int64Array*>(arr)) {
                return a->IsNull(static_cast<int>(remaining))
                           ? 0
                           : a->Value(static_cast<int>(remaining));
            }
            if (const auto* a = dynamic_cast<arrow::Int32Array*>(arr)) {
                return a->IsNull(static_cast<int>(remaining))
                           ? 0
                           : a->Value(static_cast<int>(remaining));
            }
            return 0;
        }
        remaining -= len;
    }
    return 0;
}

/// 取浮点列当前行值（machine_ts）。null 视为 0。
double getDouble(const std::shared_ptr<arrow::ChunkedArray>& col, i64 row)
{
    i64 remaining = row;
    for (const auto& chunk : col->chunks()) {
        const auto len = chunk->length();
        if (remaining < len) {
            const auto* arr = chunk.get();
            if (const auto* a = dynamic_cast<arrow::DoubleArray*>(arr)) {
                return a->IsNull(static_cast<int>(remaining)) ? 0.0
                                                              : a->Value(static_cast<int>(remaining));
            }
            if (const auto* a = dynamic_cast<arrow::FloatArray*>(arr)) {
                return a->IsNull(static_cast<int>(remaining)) ? 0.0
                                                              : a->Value(static_cast<int>(remaining));
            }
            return 0.0;
        }
        remaining -= len;
    }
    return 0.0;
}

/// 解析原始 trace 中的十六进制参数串（arg1/arg2），如 "0x3" / "0xa"
i64 parseHex(std::string_view s)
{
    if (s.empty()) {
        return 0;
    }
    // 跳过可选的 "0x" 前缀
    if (s.starts_with("0x") || s.starts_with("0X")) {
        s.remove_prefix(2);
    }
    i64 value = 0;
    for (char c : s) {
        int digit = 0;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
        else break;   // 遇到非十六进制字符停止
        value = value * 16 + digit;
    }
    return value;
}

Side parseSide(std::string_view s)
{
    if (s == "source") return Side::Source;
    if (s == "target") return Side::Target;
    return Side::Other;
}

/// 在 RecordBatch 的 schema 中按名查列，返回列索引；缺失返回 -1。
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
// Impl：封装 Arrow/Parquet 读取状态，隔离头文件依赖
// ---------------------------------------------------------------------------
struct ParquetEventReader::Impl {
    std::shared_ptr<arrow::Table>           table;       // 整桶读入内存（trace 桶通常数百 MB，可接受）
    std::shared_ptr<arrow::Schema>          schema;
    i64                                     numRows {0};
    i64                                     curRow  {0};

    // 缓存常用列的 ChunkedArray，避免每次按名查找
    std::shared_ptr<arrow::ChunkedArray> machineTs;
    std::shared_ptr<arrow::ChunkedArray> logOffset;
    std::shared_ptr<arrow::ChunkedArray> bucketCol;
    std::shared_ptr<arrow::ChunkedArray> matched;
    std::shared_ptr<arrow::ChunkedArray> ts;
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

    // 持有本行字符串字段的稳定副本：Parquet 的 string 列跨行复用同一 chunk 缓冲，
    // 但我们返回 string_view 给上层，必须保证在 next() 之间不被覆盖。Arrow 的
    // ChunkedArray 缓冲在 Table 存活期内稳定，故只要 Table 不释放即可。
};

ParquetEventReader::ParquetEventReader(fs::path bucketDir, long bucket)
    : m_bucket(bucket)
    , m_impl(std::make_unique<Impl>())
{
    // 收集桶目录下所有 .parquet 文件（rawproc 可能在单桶内输出多个 part 文件）
    std::vector<std::string> partFiles;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator{bucketDir, ec}) {
        if (entry.is_regular_file() && entry.path().extension() == ".parquet") {
            partFiles.push_back(entry.path().string());
        }
    }

    // 单桶多文件用 ParquetFileReader 逐个读，再 ConcatenateTables 合并。
    // 这里简化为：用 arrow::io 挨个打开，parquet::arrow::FileReader 读成 Table，
    // 最后 concatenate。若桶内只有一个 part，则直接用。
    std::vector<std::shared_ptr<arrow::Table>> tables;
    tables.reserve(partFiles.size());
    for (const auto& f : partFiles) {
        auto result = arrow::io::ReadableFile::Open(f);
        if (!result.ok()) {
            // 打开失败记录但不中断，由后续 numRows 判断
            continue;
        }
        std::unique_ptr<parquet::arrow::FileReader> reader;
        auto st = parquet::arrow::OpenFile(result.ValueOrDie(), arrow::default_memory_pool(), &reader);
        if (!st.ok()) {
            continue;
        }
        std::shared_ptr<arrow::Table> t;
        st = reader->ReadTable(&t);
        if (st.ok() && t) {
            tables.push_back(std::move(t));
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

        // 列名对齐 rawproc EVENT_COLS
        auto bind = [&](std::shared_ptr<arrow::ChunkedArray>& dst, std::string_view name) {
            int idx = findColumn(m_impl->schema, name);
            if (idx >= 0) {
                dst = m_impl->table->column(idx);
            }
        };
        bind(m_impl->machineTs,     "machine_ts");
        bind(m_impl->logOffset,     "log_offset");
        bind(m_impl->bucketCol,     "bucket");
        bind(m_impl->matched,       "matched");
        bind(m_impl->ts,            "ts");
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
    if (!m_impl->table) {
        // 桶内无可用数据，视为读完
        return std::optional<TraceEvent>{};
    }
    if (m_impl->curRow >= m_impl->numRows) {
        return std::optional<TraceEvent>{};
    }

    const i64 row = m_impl->curRow++;
    auto& c = m_impl;

    TraceEvent ev;
    ev.bucket     = m_bucket;
    ev.machineTs  = c->machineTs     ? getDouble(c->machineTs, row) : 0.0;
    ev.logOffset  = c->logOffset     ? getInt64(c->logOffset, row)  : 0;
    ev.pid        = c->pid           ? getInt64(c->pid, row)        : 0;
    ev.ret        = c->ret           ? getInt64(c->ret, row)        : -1;
    ev.err        = c->err           ? getInt64(c->err, row)        : 0;

    if (c->comm)          ev.comm          = sliceString(c->comm, row);
    if (c->sc)            ev.sc            = sliceString(c->sc, row);
    if (c->path)          ev.path          = sliceString(c->path, row);
    if (c->renameSrc)     ev.renameSrc     = sliceString(c->renameSrc, row);
    if (c->renameDst)     ev.renameDst     = sliceString(c->renameDst, row);
    if (c->canonicalPath) ev.canonicalPath = sliceString(c->canonicalPath, row);
    // 不信赖 rawproc 的 op_class 列，按 sc 重新分类，保持本工具自洽
    ev.opClass = classifyOp(std::string{ev.sc});

    // arg1/arg2 在原始 trace 是十六进制串，解析为数值。对 IO 操作 arg1=fd。
    if (c->arg1) ev.arg1Num = parseHex(sliceString(c->arg1, row));
    if (c->arg2) ev.arg2Num = parseHex(sliceString(c->arg2, row));

    if (c->mapped) {
        // mapped 是 boolean 列；这里用是否为 true 粗判（取 int 非零）
        ev.mapped = getInt64(c->mapped, row) != 0;
    }
    if (c->side) {
        ev.side = parseSide(sliceString(c->side, row));
    }

    return std::optional<TraceEvent>{std::move(ev)};
}

}  // namespace trace_replay
