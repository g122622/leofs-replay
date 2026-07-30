#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/Types.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace trace_replay {

// ============================================================================
// fd table — the core of openat-replay
//
// fd semantics in real rawproc output (verified):
//   * openat's ret = the original fd (a small integer); arg1 = flags (decimal).
//   * read/write's arg1 = the requested byte count (not fd); the fd info is not
//     in any arg. Its path column is a resolved full path in the vast majority
//     of cases, with a few in the form "/[unknown, fd=N]".
//   * close's arg1/ret are both 0; the fd likewise comes from path.
//
// Hence the fd tracking strategy:
//   * openat success → build the table with (pid, origFd=ret) and record path;
//   * read/write/close → prefer path (canonical_path first) to reverse-look up
//     ourFd in the same pid's table (same path = same fd); when path is
//     "/[unknown, fd=N]", extract N and directly look up (pid, N).
//
// Two indices are maintained: the fd index (origFd→entry) and the path index
// (canonicalPath→entry), updated in sync on openat registration and close
// unregistration. The path index allows the same path to be opened multiple
// times (multiple fds pointing to the same path); the most recently registered
// ourFd is taken.
// ============================================================================

/**
 * @brief A single fd mapping entry
 */
struct FdEntry {
    Fd  ourFd {-1};          // the fd actually opened by this replay process (-1 = not in use)
    std::string path;        // canonical_path (the original unified path before sandbox resolution), used for reverse-lookup
    bool isDirectory {false};
};

/**
 * @brief per-pid fd mapping table, supporting direct fd lookup and path reverse-lookup
 */
class FdTable {
public:
    FdTable() = default;

    /**
     * @brief Register a successful open
     *
     * @param pid      original process id
     * @param origFd   the fd returned by openat in the original trace (i.e. the
     *                 lookup key for this fd)
     * @param ourFd    the fd actually opened by this process
     * @param path     canonical_path (used for IO/close reverse-lookup)
     * @param isDir    whether it is a directory
     */
    void registerFd(i64 pid, Fd origFd, Fd ourFd, std::string path, bool isDir);

    /**
     * @brief Unregister an original fd (direct fd-lookup path, for "/[unknown, fd=N]")
     *
     * @return this process's ourFd (for the caller to close); nullopt means the
     *         (pid,origFd) was not registered
     */
    [[nodiscard]] std::optional<Fd> unregisterFd(i64 pid, Fd origFd);

    /**
     * @brief Reverse-lookup by path and unregister (for close with a resolved path)
     *
     * Takes the most recently registered ourFd for that path under this pid and
     * unregisters it. Returns ourFd for closing; nullopt means the (pid,path)
     * was not registered.
     */
    [[nodiscard]] std::optional<Fd> unregisterByPath(i64 pid, std::string_view path);

    /// Direct lookup by fd (used by IO/close in the "/[unknown, fd=N]" case)
    [[nodiscard]] const FdEntry* lookup(i64 pid, Fd origFd) const;

    /// Reverse-lookup by path (used by IO/close when path is resolved)
    [[nodiscard]] const FdEntry* lookupByPath(i64 pid, std::string_view path) const;

    /// Close and clear all mappings for a pid (called when the process exits)
    void closePid(i64 pid);

    /// Total number of currently registered mappings (diagnostic)
    [[nodiscard]] size_t size() const noexcept;

private:
    struct PerPid {
        std::unordered_map<Fd, FdEntry> byFd;
        // path → most recently registered origFd (points to an entry in byFd)
        std::unordered_map<std::string, Fd> byPath;
    };
    std::unordered_map<i64, PerPid> m_table;
};

}  // namespace trace_replay
