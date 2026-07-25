#pragma once

#include "trace_replay/core/TraceEvent.hpp"

#include <string_view>

namespace trace_replay {

// ============================================================================
// syscall 分类表
//
// 忠实移植 rawproc_spark.py 中的 PATH_OPS / IO_OPS / RENAME_SC（见
// context.md）。replay 引擎据此决定：meta 类走 path 解析并真实打开/查询，
// IO 类走 fd 表（以 fd 为准）。
// ============================================================================

/// 判断 syscall 是否为路径类元数据操作（PATH_OPS）
[[nodiscard]] bool isPathOp(std::string_view sc) noexcept;

/// 判断 syscall 是否为 IO 类操作（IO_OPS，以 fd 为准）
[[nodiscard]] bool isIoOp(std::string_view sc) noexcept;

/// 判断 syscall 是否为 rename 系列（RENAME_SC）
[[nodiscard]] bool isRenameOp(std::string_view sc) noexcept;

/// 由 sc 推导 OpClass（对齐 rawproc 的 op_class 判定）
[[nodiscard]] OpClass classifyOp(std::string_view sc) noexcept;

/// 该 syscall 是否需要用 dirfd+path 真实打开文件（openat/open/openat2）
[[nodiscard]] bool isOpenOp(std::string_view sc) noexcept;

}  // namespace trace_replay
