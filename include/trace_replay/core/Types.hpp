#pragma once

// ============================================================================
// 基础类型别名
//
// 与 Cubium 主体一致使用固定宽度整数别名，避免在不同平台上 int/long 宽度
// 不一致带来的解析歧义。trace 中 ts 是浮点（machine_ts），log_offset/pid 是
// 整型，故这里给出对应别名。
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

// trace 中 fd 即非负 int；arg1/arg2 在原始 trace 中是十六进制字符串，
// 解析后用 i64 容纳（Linux 下 fd/offset 都能放进 i64）
using Fd  = i32;

}  // namespace trace_replay
