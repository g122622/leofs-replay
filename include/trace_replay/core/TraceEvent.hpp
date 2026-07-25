#pragma once

#include "trace_replay/core/Types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace trace_replay {

// ============================================================================
// 单条 trace 事件
//
// 字段对齐 rawproc 的 EVENT_COLS（见 context.md）。rawproc 已按
// (machine_ts, log_offset) 排序，本结构即 replay 引擎消费的单元。
//
// 关键约定（与 rawproc 分工一致）：
//   * IO 类操作（read/write/...）的 path 列不可信，replay 时以 (pid, arg1)
//     即 fd 为准 —— arg1 在原始 trace 中是十六进制字符串，这里解析成数值后
//     存入 arg1Num/arg2Num，便于 fd 表查找。这就是 openat-replay 的核心。
//   * meta 类操作（openat/stat/rename/...）携带可信 path，用于真实打开/查询。
// ============================================================================

/// 操作大类，对齐 rawproc 的 op_class
enum class OpClass : u8 {
    Unparsed,   // 无法解析的原始行（_unparsed 桶）
    Meta,       // 路径类元数据操作：openat/stat/rename/mkdir/unlink/...
    Io,         // IO 类操作：read/write/pread64/...（以 fd 为准）
    Other,      // 其它有 sc 但非 meta/io
};

/// 迁移侧标记，对齐 rawproc 的 side 列（source/target/other）
enum class Side : u8 {
    Other,
    Source,
    Target,
};

/**
 * @brief 单条 trace 事件
 *
 * 轻量值类型。为兼顾两种读取场景：
 *   * Parquet reader 把整桶读入内存，string 列缓冲在 Table 存活期内稳定，
 *     故 string_view 字段可安全跨多次 next() 使用；
 *   * 但归并器与调用方往往需要"拥有"事件（跨 reader 推进后仍有效）。
 * 因此对外统一返回拥有字符串所有权的副本（见 EventMerger 的 materialize）。
 * 内部 reader 阶段可临时用 string_view 指向缓冲，拷出时再物化。
 */
struct TraceEvent {
    // —— 排序键（rawproc 已据此排序，replay 顺序消费即可）——
    double machineTs {0.0};   // 机器时间戳（raw ts），全局排序主键
    i64    logOffset {0};     // 原始字节偏移，稳定 tie-breaker
    i64    bucket    {0};     // 所属桶编号 = floor(machineTs / 600)

    // —— 进程标识 ——
    i64 pid {0};

    // —— syscall 标识与分类 ——
    std::string comm;        // 进程命令名（仅诊断用）
    std::string sc;          // syscall 名，如 openat/read
    OpClass     opClass {OpClass::Other};

    // —— 返回值/错误码 ——
    i64 ret {-1};             // syscall 返回值
    i64 err {0};              // errno（0 表示成功）

    // —— 参数 ——
    // arg1/arg2 在原始 trace 中是十六进制字符串。对 IO 操作，arg1 即 fd；
    // 对 openat，arg1 是 dirfd、arg2 是 flags。这里解析成数值便于使用。
    i64 arg1Num {0};
    i64 arg2Num {0};

    // —— 路径（仅 meta 类操作可信）——
    std::string path;        // 原始 path 列
    std::string renameSrc;   // rename 的源路径（若为 rename）
    std::string renameDst;   // rename 的目标路径（若为 rename）
    std::string canonicalPath;  // canonical_path 列（rawproc 已映射）
    bool mapped {false};          // 是否映射到 canonical 命名空间

    Side side {Side::Other};

    // —— 便利判定（基于 sc 的常见判别，避免在多处重复字符串比较）——
    [[nodiscard]] bool isIo() const noexcept { return opClass == OpClass::Io; }
    [[nodiscard]] bool isMeta() const noexcept { return opClass == OpClass::Meta; }
    [[nodiscard]] bool isRename() const noexcept { return !renameDst.empty(); }
};

}  // namespace trace_replay
