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
// 列访问辅助：Arrow 的 ChunkedArray 由多个 chunk 组成，跨 chunk 取第 row 行。
// 所有访问都走 const 指针（chunks() 返回 const 数组），避免 const 丢失。
// ---------------------------------------------------------------------------

/// 取字符串列第 row 行的视图，指向 Arrow 内部缓冲（Table 存活期内稳定）
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

/// 取整数列第 row 行值（int64/int32）。null 视为 0。
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

/// 取浮点列第 row 行值（machine_ts）。null 视为 0。
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
                // 显式转 double，避免 -Wdouble-promotion（float→double 精度提升）
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

/// 解析原始 trace 中的数值参数串。
/// rawproc 把 ret/err/arg1/arg2 存为【十进制】字符串（如 openat 的 flags
/// "524288"=0x80000=O_CLOEXEC，read 的字节数 "32768"）。仅当带 "0x" 前缀时
/// 才按十六进制解析，兼容极少数原始行。
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

/// 在 schema 中按名查列索引；缺失返回 -1
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
    std::shared_ptr<arrow::Table>           table;
    std::shared_ptr<arrow::Schema>          schema;
    i64                                     numRows {0};
    i64                                     curRow  {0};

    // 缓存常用列，避免每次按名查找
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
    // 收集待读的 .parquet 文件。支持两种输入：
    //   1) bucketDir 是单个 .parquet 文件 —— 单文件直读模式（用户提供的扁平
    //      part-xxxx.parquet，已全局排序，无需分桶）；
    //   2) bucketDir 是目录 —— rawproc 桶目录，枚举其下所有 .parquet part 文件。
    // 路径以 UTF-8 形式存入 partFiles：Arrow 的 std::string 路径 API 在所有平台
    // 均按 UTF-8 解释，Windows 上若用 path::string()（ACP 编码）会损坏含中文的路径。
    auto toUtf8 = [](const fs::path& p) -> std::string {
        auto u8 = p.u8string();   // std::u8string，UTF-8 编码
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

    // 逐个用 parquet::arrow::OpenFile + ReadTable 读成 Table，再合并
    std::vector<std::shared_ptr<arrow::Table>> tables;
    tables.reserve(partFiles.size());
    for (const auto& f : partFiles) {
        auto opened = arrow::io::ReadableFile::Open(f);
        if (!opened.ok()) {
            continue;   // 打开失败记录但不中断，由后续 numRows 判断
        }
        // Arrow 24：parquet::arrow::OpenFile 返回 Result<unique_ptr<FileReader>>
        auto readerResult = parquet::arrow::OpenFile(opened.ValueOrDie(),
                                                     arrow::default_memory_pool());
        if (!readerResult.ok()) {
            continue;
        }
        auto& reader = *readerResult;
        // ReadTable() 返回 arrow::Result<shared_ptr<Table>>
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

        // 列名对齐 rawproc EVENT_COLS
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

    // ret/err/arg1/arg2 在 rawproc 输出中是【字符串列】（十进制），这里解析。
    //   openat: ret=fd(小整数), arg1=flags(如 524288=O_CLOEXEC)
    //   read/write: ret=实际字节数, arg1=请求字节数(count), fd 不在此处
    //   close: ret=0, fd 不在此处
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
    // 不信赖 rawproc 的 op_class 列，按 sc 重新分类，保持本工具自洽
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
