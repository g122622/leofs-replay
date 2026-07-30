#include "trace_replay/model/PathResolver.hpp"

#include "trace_replay/core/Assert.hpp"
#include "trace_replay/core/Error.hpp"

#include <filesystem>
#include <system_error>

namespace trace_replay {
namespace {

namespace fs = std::filesystem;

/// Take the event's trusted path: prefer canonical_path, then path
std::string_view bestPath(const TraceEvent& ev)
{
    if (ev.mapped && !ev.canonicalPath.empty()) {
        return ev.canonicalPath;
    }
    return ev.path;
}

}  // namespace

PathResolver::PathResolver(fs::path sandboxRoot)
    : m_root(std::move(sandboxRoot))
{
    TR_ASSERT(!m_root.empty());
}

Result<fs::path> PathResolver::resolve(const TraceEvent& ev, const FdTable& fdTbl) const
{
    std::string_view raw = bestPath(ev);
    // For rename, the path is of the form "src -> dst"; take the target side
    // (dst) as the operation path here. Source/target are resolved separately
    // by the executor when needed.
    if (ev.isRename()) {
        raw = ev.renameDst.empty() ? raw : std::string_view{ev.renameDst};
    }
    if (raw.empty()) {
        return Error::invalidArgument("event has no resolvable path", "PathResolver::resolve");
    }

    fs::path target;
    if (raw.front() == '/') {
        // Absolute path: strip the leading '/' and append under the sandbox
        // root, to avoid hitting the real root directly.
        std::string_view rel = raw;
        rel.remove_prefix(1);
        target = m_root / fs::path{std::string{rel}};
    } else {
        // Relative path: decide the base directory by dirfd
        const Fd dirfd = static_cast<Fd>(ev.arg1Num);
        if (dirfd == AT_FDCWD_VALUE) {
            target = m_root / fs::path{std::string{raw}};
        } else {
            const FdEntry* base = fdTbl.lookup(ev.pid, dirfd);
            if (!base) {
                return Error::notFound(
                    "dirfd of a relative path is not registered in the fd table: pid=" + std::to_string(ev.pid) +
                    " dirfd=" + std::to_string(dirfd),
                    "PathResolver::resolve");
            }
            target = fs::path{base->path} / fs::path{std::string{raw}};
        }
    }

    // Normalize and enforce traversal protection: the result must be under the
    // sandbox root (or equal to it).
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(target, ec);
    if (ec) {
        return Error::invalidArgument(
            "path normalization failed: " + target.string() + " : " + ec.message(),
            "PathResolver::resolve");
    }

    // Check whether canonical has m_root as a prefix. After weakly_canonical
    // both are in canonical form.
    auto rootCanon = fs::weakly_canonical(m_root, ec);
    if (ec) {
        return Error::invalidArgument(
            "sandbox root normalization failed: " + m_root.string(),
            "PathResolver::resolve");
    }

    // Use lexically_relative to test containment: if canonical relative to
    // rootCanon does not start with "..", then canonical is under root.
    auto rel = canonical.lexically_relative(rootCanon);
    bool escapes = false;
    for (const auto& part : rel) {
        if (part == "..") { escapes = true; break; }
    }
    if (escapes) {
        return Error::invalidArgument(
            "path traversal rejected: " + target.string() + " -> " + canonical.string(),
            "PathResolver::resolve");
    }

    return canonical;
}

}  // namespace trace_replay
