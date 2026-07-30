#pragma once

// ============================================================================
// Basic type aliases
//
// Like the Cubium main project, fixed-width integer aliases are used to avoid
// parsing ambiguity from platform-dependent int/long widths. In the trace, ts
// is a float (machine_ts), while log_offset/pid are integers, so the matching
// aliases are provided here.
// ============================================================================

#include <cstdint>

namespace trace_replay {

using i8  = std::int8_t;
using u8  = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;

// In the trace, fd is a non-negative int; arg1/arg2 are hex strings in the
// original trace, parsed into i64 here (on Linux both fd and offset fit in i64).
using Fd  = i32;

}  // namespace trace_replay
