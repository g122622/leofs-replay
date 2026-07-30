#pragma once

#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/core/Types.hpp"

#include <optional>
#include <string_view>

namespace trace_replay {

// ============================================================================
// Event path/fd utilities
//
// Supports the FdTable's path reverse-lookup strategy:
//   * bestLookupPath(ev) — the unified path used for fd reverse-lookup:
//     prefer canonical_path, fall back to path. read/write/close all use it to
//     reverse-look up ourFd in the fd table.
//   * extractUnknownFd(path) — extract N from the "/[unknown, fd=N]" form, used
//     as a direct fd-lookup fallback when the path cannot be parsed.
// ============================================================================

/// The unified path used for fd reverse-lookup (canonical_path preferred, else path)
[[nodiscard]] inline std::string_view bestLookupPath(const TraceEvent& ev)
{
    if (ev.mapped && !ev.canonicalPath.empty()) {
        return ev.canonicalPath;
    }
    return ev.path;
}

/// Extract fd=N from the "/[unknown, fd=N]" form; returns nullopt if not that form
[[nodiscard]] inline std::optional<Fd> extractUnknownFd(std::string_view path)
{
    // Of the form "/[unknown, fd=26]" — locate the digits after "fd="
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
