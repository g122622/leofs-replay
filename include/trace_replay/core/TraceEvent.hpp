#pragma once

#include "trace_replay/core/Types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace trace_replay {

// ============================================================================
// A single trace event
//
// Fields are aligned with rawproc's EVENT_COLS (see context.md). rawproc has
// already sorted by (machine_ts, log_offset); this struct is the unit consumed
// by the replay engine.
//
// Key conventions (consistent with the rawproc division of labor):
//   * For IO operations (read/write/...) the path column is unreliable; during
//     replay (pid, arg1) — i.e. fd — is authoritative. arg1 is a hex string in
//     the original trace; here it is parsed into a number and stored in
//     arg1Num/arg2Num for fd-table lookup. This is the core of openat-replay.
//   * Meta operations (openat/stat/rename/...) carry a trusted path, used for
//     real open/query.
// ============================================================================

/// Operation category, aligned with rawproc's op_class
enum class OpClass : u8 {
    Unparsed,   // unparseable raw line (_unparsed bucket)
    Meta,       // path-kind metadata operation: openat/stat/rename/mkdir/unlink/...
    Io,         // IO operation: read/write/pread64/... (fd is authoritative)
    Other,      // other syscalls that are neither meta nor io
};

/// Migration side marker, aligned with rawproc's side column (source/target/other)
enum class Side : u8 {
    Other,
    Source,
    Target,
};

/**
 * @brief A single trace event
 *
 * A lightweight value type. To serve two read scenarios:
 *   * The Parquet reader loads a whole bucket into memory; string-column
 *     buffers stay valid for the lifetime of the Table, so string_view fields
 *     can safely span multiple next() calls;
 *   * but the merger and callers often need to "own" an event (still valid
 *     after the reader advances).
 * Therefore the public API uniformly returns an owning copy of the strings (see
 * EventMerger's materialize). Internally, the reader stage may temporarily use
 * a string_view pointing into a buffer, materializing it on copy-out.
 */
struct TraceEvent {
    // —— Sort keys (rawproc already sorted by these; replay consumes in order) ——
    double machineTs {0.0};   // machine timestamp (raw ts); global sort primary key
    i64    logOffset {0};     // original byte offset; stable tie-breaker
    i64    bucket    {0};     // owning bucket number = floor(machineTs / 600)

    // —— Process identity ——
    i64 pid {0};

    // —— syscall identity and classification ——
    std::string comm;        // process command name (diagnostic only)
    std::string sc;          // syscall name, e.g. openat/read
    OpClass     opClass {OpClass::Other};

    // —— Return value / error code ——
    i64 ret {-1};             // syscall return value
    i64 err {0};              // errno (0 means success)

    // —— Arguments ——
    // arg1/arg2 are hex strings in the original trace. For IO operations, arg1
    // is fd; for openat, arg1 is dirfd and arg2 is flags. Parsed into numbers
    // here for convenience.
    i64 arg1Num {0};
    i64 arg2Num {0};

    // —— Path (trusted only for meta operations) ——
    std::string path;        // original path column
    std::string renameSrc;   // rename source path (if this is a rename)
    std::string renameDst;   // rename target path (if this is a rename)
    std::string canonicalPath;  // canonical_path column (already mapped by rawproc)
    bool mapped {false};         // whether mapped to the canonical namespace

    Side side {Side::Other};

    // —— Convenience predicates (common sc-based checks, avoiding repeated
    //    string comparisons in multiple places) ——
    [[nodiscard]] bool isIo() const noexcept { return opClass == OpClass::Io; }
    [[nodiscard]] bool isMeta() const noexcept { return opClass == OpClass::Meta; }
    [[nodiscard]] bool isRename() const noexcept { return !renameDst.empty(); }
};

}  // namespace trace_replay
