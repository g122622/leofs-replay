#include "trace_replay/replay/SyscallExecutor.hpp"

#include "trace_replay/core/Error.hpp"
#include "trace_replay/core/SyscallClassify.hpp"
#include "trace_replay/core/TraceEventUtil.hpp"

#include <filesystem>
#include <format>
#include <ostream>
#include <string>

#ifndef _WIN32
// ============================================================================
// POSIX implementation: really executes syscalls on Linux/macOS.
// Real replay depends on POSIX interfaces such as openat/read/write/rename; the
// trace itself is a Linux bpftrace product, so it builds and runs on non-Windows
// platforms.
// ============================================================================
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

namespace trace_replay {
namespace {

namespace fs = std::filesystem;

ExecOutcome skipped(std::string why)
{
    return ExecOutcome{true, true, std::move(why)};
}

/// Map Linux open flags to open flags usable by this process.
/// We do not need to faithfully reproduce the original flags; we only need to
/// "open in a reasonable way" to carry subsequent IO.
///
/// Real rawproc output (verified): openat's arg1 = flags (a [decimal] integer),
/// e.g. 524288 = 0x80000 = O_CLOEXEC. These Linux values are parsed bitwise
/// here:
///   access mode, low two bits: O_RDONLY=0 O_WRONLY=1 O_RDWR=2
///   O_CREAT  = 0100(0x40)   O_EXCL   = 0200(0x80)
///   O_TRUNC  = 01000(0x200) O_APPEND = 02000(0x400)
///   O_CLOEXEC= 02000000(0x80000) — preserved if present (this process always
///   adds CLOEXEC)
int translateOpenFlags(i64 origFlags)
{
    int flags = O_CLOEXEC;   // the replay process defaults to close-on-exec to
                             // avoid fd leakage
    // Access mode: take the low two bits
    switch (origFlags & 0x3) {
        case 0x1: flags |= O_WRONLY; break;
        case 0x2: flags |= O_RDWR;   break;
        default: flags |= O_RDONLY;  break;
    }
    if (origFlags & 0x40)    flags |= O_CREAT;
    if (origFlags & 0x80)    flags |= O_EXCL;
    if (origFlags & 0x200)   flags |= O_TRUNC;
    if (origFlags & 0x400)   flags |= O_APPEND;
    // O_CLOEXEC(0x80000): this process already has it by default; nothing extra
    return flags;
}

}  // namespace

SyscallExecutor::SyscallExecutor(fs::path sandboxRoot, i64 maxIoBytes,
                                 bool continueOnError, std::ostream& log)
    : m_resolver(std::move(sandboxRoot))
    , m_maxIoBytes(maxIoBytes)
    , m_continueOnError(continueOnError)
    , m_log(log) {}

Result<ExecOutcome> SyscallExecutor::execute(const TraceEvent& ev)
{
    if (isOpenOp(ev)) {
        return doOpen(ev);
    }
    if (ev.sc == "close") {
        return doClose(ev);
    }
    if (ev.isIo()) {
        return doIo(ev);
    }
    if (ev.sc == "stat" || ev.sc == "lstat" || ev.sc == "newfstatat" ||
        ev.sc == "statx") {
        return doStatLike(ev);
    }
    if (ev.sc == "mkdir" || ev.sc == "mkdirat") {
        return doMkdir(ev);
    }
    if (ev.sc == "unlink" || ev.sc == "unlinkat") {
        return doUnlink(ev);
    }
    if (ev.isRename()) {
        return doRename(ev);
    }
    // getdents/utimensat/xattr etc. are not yet executed for real; mark as
    // skipped.
    return skipped(std::format("not-yet-implemented syscall: {}", ev.sc));
}

Result<ExecOutcome> SyscallExecutor::doOpen(const TraceEvent& ev)
{
    // Resolve the sandbox path (including traversal protection)
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));

    // Real rawproc output: openat's arg1 = flags (decimal), arg2 = mode etc.
    // So flags come from arg1Num.
    const int flags = translateOpenFlags(ev.arg1Num);
    const mode_t mode = (flags & O_CREAT) ? 0644 : 0;

    int ourFd = ::open(resolved.c_str(), flags, mode);
    if (ourFd < 0) {
        const int e = errno;
        // If the original open itself failed (ret<0), "reproducing the failure"
        // is not our error.
        if (ev.ret < 0) {
            return ExecOutcome{true, false,
                std::format("open reproduced original failure: {} (errno={}, orig ret={})",
                            resolved.string(), e, ev.ret)};
        }
        return Error::syscallError(
            std::format("open failed: {} : {}", resolved.string(), std::strerror(e)),
            "SyscallExecutor::doOpen");
    }

    if (ev.ret >= 0) {
        struct stat st {};
        bool isDir = (::fstat(ourFd, &st) == 0) && S_ISDIR(st.st_mode);
        // Register using bestLookupPath (canonical_path preferred) as the
        // reverse-lookup key, consistent with IO/close.
        m_fdTable.registerFd(ev.pid, static_cast<Fd>(ev.ret), ourFd,
                             std::string{bestLookupPath(ev)}, isDir);
    } else {
        // Original failed but we opened it: to keep the fd table consistent with
        // the original, close our fd immediately.
        ::close(ourFd);
    }
    return ExecOutcome{true, false,
        std::format("open {} -> ourFd={} (origFd={})", resolved.string(), ourFd, ev.ret)};
}

Result<ExecOutcome> SyscallExecutor::doIo(const TraceEvent& ev)
{
    // fd tracking uses path reverse-lookup: read/write's arg1 is the byte count,
    // not fd. Prefer bestLookupPath (canonical_path preferred, falling back to
    // path) to reverse-look up ourFd under this pid; if path is in the
    // "/[unknown, fd=N]" form, extract N and fall back to a direct fd lookup.
    const std::string_view lookupPath = bestLookupPath(ev);
    const FdEntry* e = nullptr;
    if (auto fd = extractUnknownFd(lookupPath)) {
        e = m_fdTable.lookup(ev.pid, *fd);
    } else {
        e = m_fdTable.lookupByPath(ev.pid, lookupPath);
    }
    if (!e) {
        return skipped(std::format("IO fd mapping not found: pid={} path=\"{}\"", ev.pid, lookupPath));
    }

    // Original ret = actual bytes read/written. Reproduce the time-series
    // semantics of "N bytes of IO occurred on this fd"; do not reproduce data
    // content (the trace has no payload). read uses a zero buffer, write writes
    // zero bytes as a placeholder.
    // arg1Num = requested byte count (pread64/pwrite64's offset is in arg2Num).
    const i64 wantBytes = ev.ret > 0 ? ev.ret : 0;
    const i64 ioLen = std::min(wantBytes, m_maxIoBytes);
    std::vector<char> buf(static_cast<size_t>(ioLen), 0);

    if (ev.sc == "read" || ev.sc == "pread64") {
        if (ev.sc == "pread64") {
            ::pread64(e->ourFd, buf.data(), buf.size(), ev.arg2Num);
        } else {
            ::read(e->ourFd, buf.data(), buf.size());
        }
    } else if (ev.sc == "write" || ev.sc == "pwrite64") {
        if (ev.sc == "pwrite64") {
            ::pwrite64(e->ourFd, buf.data(), buf.size(), ev.arg2Num);
        } else {
            ::write(e->ourFd, buf.data(), buf.size());
        }
    } else if (ev.sc == "readv" || ev.sc == "writev") {
        iovec iov{buf.data(), buf.size()};
        if (ev.sc == "readv") ::readv(e->ourFd, &iov, 1);
        else                  ::writev(e->ourFd, &iov, 1);
    }
    return ExecOutcome{true, false,
        std::format("{} {} bytes via ourFd={} ({})", ev.sc, ioLen, e->ourFd, e->path)};
}

Result<ExecOutcome> SyscallExecutor::doClose(const TraceEvent& ev)
{
    // close's arg1/ret are both 0; the fd info is not in any arg, so path
    // reverse-lookup is used here too.
    const std::string_view lookupPath = bestLookupPath(ev);
    std::optional<Fd> ourFd;
    if (auto fd = extractUnknownFd(lookupPath)) {
        ourFd = m_fdTable.unregisterFd(ev.pid, *fd);
    } else {
        ourFd = m_fdTable.unregisterByPath(ev.pid, lookupPath);
    }
    if (!ourFd) {
        return skipped(std::format("close fd mapping not found: pid={} path=\"{}\"", ev.pid, lookupPath));
    }
    if (::close(*ourFd) < 0) {
        const int e = errno;
        return Error::syscallError(
            std::format("close failed: ourFd={} : {}", *ourFd, std::strerror(e)),
            "SyscallExecutor::doClose");
    }
    return ExecOutcome{true, false,
        std::format("close ourFd={} (path=\"{}\")", *ourFd, lookupPath)};
}

Result<ExecOutcome> SyscallExecutor::doStatLike(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    struct stat st {};
    if (::stat(resolved.c_str(), &st) < 0) {
        const int e = errno;
        return skipped(std::format("{} target does not exist: {} (errno={})", ev.sc, resolved.string(), e));
    }
    return ExecOutcome{true, false,
        std::format("{} -> {} (size={})", ev.sc, resolved.string(), st.st_size)};
}

Result<ExecOutcome> SyscallExecutor::doMkdir(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    if (::mkdir(resolved.c_str(), 0755) < 0) {
        const int e = errno;
        if (e == EEXIST) {
            return ExecOutcome{true, false, std::format("mkdir already exists: {}", resolved.string())};
        }
        return Error::syscallError(
            std::format("mkdir failed: {} : {}", resolved.string(), std::strerror(e)),
            "SyscallExecutor::doMkdir");
    }
    return ExecOutcome{true, false, std::format("mkdir -> {}", resolved.string())};
}

Result<ExecOutcome> SyscallExecutor::doUnlink(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    if (::unlink(resolved.c_str()) < 0) {
        const int e = errno;
        if (e == ENOENT) {
            return skipped(std::format("unlink target does not exist: {}", resolved.string()));
        }
        return Error::syscallError(
            std::format("unlink failed: {} : {}", resolved.string(), std::strerror(e)),
            "SyscallExecutor::doUnlink");
    }
    return ExecOutcome{true, false, std::format("unlink -> {}", resolved.string())};
}

Result<ExecOutcome> SyscallExecutor::doRename(const TraceEvent& ev)
{
    // rename needs to resolve both the source and target paths.
    // PathResolver::resolve defaults to the target side; here we resolve the
    // source side separately (using renameSrc).
    TraceEvent srcEv = ev;
    srcEv.path = ev.renameSrc;
    srcEv.renameDst.clear();
    TR_TRY(srcResolved, m_resolver.resolve(srcEv, m_fdTable));
    TR_TRY(dstResolved, m_resolver.resolve(ev, m_fdTable));

    std::error_code ec;
    fs::create_directories(dstResolved.parent_path(), ec);

    if (::rename(srcResolved.c_str(), dstResolved.c_str()) < 0) {
        const int e = errno;
        if (e == ENOENT) {
            return skipped(std::format("rename source does not exist: {}", srcResolved.string()));
        }
        return Error::syscallError(
            std::format("rename failed: {} -> {} : {}",
                        srcResolved.string(), dstResolved.string(), std::strerror(e)),
            "SyscallExecutor::doRename");
    }
    return ExecOutcome{true, false,
        std::format("rename {} -> {}", srcResolved.string(), dstResolved.string())};
}

#else
// ============================================================================
// Windows placeholder implementation
//
// Real syscall replay depends on POSIX interfaces (openat/pread64/rename...),
// which have no counterpart on Windows. The Windows build is only used to
// verify cross-platform logic such as parsing, sorting, the fd table, and path
// resolution (the DryRunExecutor path). On Windows, SyscallExecutor constructs
// legally but execute always returns "platform not supported", so the whole
// thing still compiles and links.
// ============================================================================

namespace trace_replay {

SyscallExecutor::SyscallExecutor(std::filesystem::path sandboxRoot, i64 maxIoBytes,
                                 bool continueOnError, std::ostream& log)
    : m_resolver(std::move(sandboxRoot))
    , m_maxIoBytes(maxIoBytes)
    , m_continueOnError(continueOnError)
    , m_log(log) {}

Result<ExecOutcome> SyscallExecutor::execute(const TraceEvent& ev)
{
    // Real syscall replay is only available on POSIX platforms; on Windows it
    // is marked as skipped.
    (void)ev;
    return ExecOutcome{true, true, "real syscall replay is not supported on Windows; use DryRunExecutor"};
}

#endif  // _WIN32

}  // namespace trace_replay
