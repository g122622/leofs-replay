#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/core/Types.hpp"
#include "trace_replay/model/FdTable.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace trace_replay {

// ============================================================================
// Path resolver
//
// Resolves an original trace path into an "absolute path inside the sandbox"
// and enforces path-traversal protection (aligned with Cubium spec 10: validate
// external input, prevent path traversal).
//
// Resolution rules:
//   1. Prefer canonical_path (rawproc already maps mounts into a unified CAPFS
//      namespace); if mapped=false or canonical_path is empty, fall back to path.
//   2. A path starting with '/' is treated as absolute and appended directly
//      under sandboxRoot; otherwise it is a relative path and needs a dirfd:
//        * dirfd == AT_FDCWD(-100) → relative to the sandbox root
//        * otherwise look up the sandbox directory path for that dirfd in the
//          fd table, then append
//   3. After normalization (weakly_canonical), assert the result is still under
//      sandboxRoot; otherwise return an InvalidArgument error and refuse to
//      execute that event.
// ============================================================================

/// The value of AT_FDCWD (Linux; for openat's dirfd meaning "relative to the
/// current working directory")
inline constexpr Fd AT_FDCWD_VALUE = -100;

/**
 * @brief Sandbox path resolver
 */
class PathResolver {
public:
    explicit PathResolver(std::filesystem::path sandboxRoot);

    /**
     * @brief Resolve the event's path into an absolute path inside the sandbox
     *
     * @param ev    the event (takes its canonical_path/path, dirfd=arg1)
     * @param fdTbl per-pid fd table, used to resolve the dirfd of a relative path
     * @return absolute path inside the sandbox; on failure returns an error
     *         (path traversal, etc.)
     */
    [[nodiscard]] Result<std::filesystem::path> resolve(
        const TraceEvent& ev, const FdTable& fdTbl) const;

    /// The sandbox root
    [[nodiscard]] const std::filesystem::path& sandboxRoot() const noexcept { return m_root; }

private:
    std::filesystem::path m_root;
};

}  // namespace trace_replay
