#pragma once

#include "trace_replay/core/TraceEvent.hpp"

#include <string_view>

namespace trace_replay {

// ============================================================================
// syscall classification table
//
// Faithfully ported from PATH_OPS / IO_OPS / RENAME_SC in rawproc_spark.py (see
// context.md). The replay engine uses it to decide: meta operations go through
// path resolution and real open/query, while IO operations go through the fd
// table (fd is authoritative).
// ============================================================================

/// Whether a syscall is a path-kind metadata operation (PATH_OPS)
[[nodiscard]] bool isPathOp(std::string_view sc) noexcept;

/// Whether a syscall is an IO operation (IO_OPS; fd is authoritative)
[[nodiscard]] bool isIoOp(std::string_view sc) noexcept;

/// Whether a syscall is a rename variant (RENAME_SC)
[[nodiscard]] bool isRenameOp(std::string_view sc) noexcept;

/// Derive the OpClass from sc (aligned with rawproc's op_class decision)
[[nodiscard]] OpClass classifyOp(std::string_view sc) noexcept;

/// Whether this syscall needs to really open a file via dirfd+path
/// (openat/open/openat2)
[[nodiscard]] bool isOpenOp(std::string_view sc) noexcept;

}  // namespace trace_replay
