#pragma once

#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/core/Types.hpp"

#include <optional>
#include <string_view>

namespace trace_replay {

// ============================================================================
// 事件路径/fd 工具
//
// 配合 FdTable 的 path 反查策略：
//   * bestLookupPath(ev) —— 取用于 fd 反查的归一路径：优先 canonical_path，
//     退化用 path。read/write/close 都用它去 fd 表反查 ourFd。
//   * extractUnknownFd(path) —— 从 "/[unknown, fd=N]" 形态提取 N，用于
//     path 不可解析时的 fd 直查回退。
// ============================================================================

/// 取事件用于 fd 反查的归一路径（canonical_path 优先，否则 path）
[[nodiscard]] inline std::string_view bestLookupPath(const TraceEvent& ev)
{
    if (ev.mapped && !ev.canonicalPath.empty()) {
        return ev.canonicalPath;
    }
    return ev.path;
}

/// 从 "/[unknown, fd=N]" 形态提取 fd=N；非该形态返回 nullopt
[[nodiscard]] inline std::optional<Fd> extractUnknownFd(std::string_view path)
{
    // 形如 "/[unknown, fd=26]" —— 定位 "fd=" 后的数字
    constexpr std::string_view marker = "fd=";
    auto pos = path.find(marker);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view num = path.substr(pos + marker.size());
    i64 value = 0;
    bool any = false;
    for (char c : num) {
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            any = true;
        } else {
            break;
        }
    }
    if (!any || value < 0) {
        return std::nullopt;
    }
    return static_cast<Fd>(value);
}

}  // namespace trace_replay
