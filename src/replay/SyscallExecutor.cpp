#include "trace_replay/replay/SyscallExecutor.hpp"

#include "trace_replay/core/Error.hpp"
#include "trace_replay/core/SyscallClassify.hpp"

#include <filesystem>
#include <format>
#include <ostream>
#include <string>

#ifndef _WIN32
// ============================================================================
// POSIX 实现：在 Linux/macOS 下真实执行 syscall。
// 真实回放依赖 openat/read/write/rename 等 POSIX 接口，trace 本身是 Linux
// bpftrace 产物，故在非 Windows 平台构建运行。
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

/// 把 Linux open flags（原始 trace arg2 的十六进制）映射为本进程可用的 open flags。
/// 我们不需要精确复刻原始 flag，只需保证"以合理方式打开"以承接后续 IO。
/// Linux 取值：O_RDONLY=0 O_WRONLY=1 O_RDWR=2；O_CREAT=0100(0x40) O_EXCL=0200(0x80)
///              O_TRUNC=01000(0x200) O_APPEND=02000(0x400)
int translateOpenFlags(i64 origFlags)
{
    int flags = O_CLOEXEC;   // 回放进程默认 close-on-exec，避免 fd 泄漏
    // 访问模式取低两位
    switch (origFlags & 0x3) {
        case 0x1: flags |= O_WRONLY; break;
        case 0x2: flags |= O_RDWR;   break;
        default: flags |= O_RDONLY;  break;
    }
    if (origFlags & 0x40)  flags |= O_CREAT;
    if (origFlags & 0x80)  flags |= O_EXCL;
    if (origFlags & 0x200) flags |= O_TRUNC;
    if (origFlags & 0x400) flags |= O_APPEND;
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
    // getdents/utimensat/xattr 等暂不真实执行，标记跳过
    return skipped(std::format("暂未实现的 syscall: {}", ev.sc));
}

Result<ExecOutcome> SyscallExecutor::doOpen(const TraceEvent& ev)
{
    // 解析沙箱内路径（含防穿越校验）
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));

    // 原始 trace 的 arg2 为 open flags（对 openat: arg1=dirfd, arg2=flags）。
    const int flags = translateOpenFlags(ev.arg2Num);
    const mode_t mode = (flags & O_CREAT) ? 0644 : 0;

    int ourFd = ::open(resolved.c_str(), flags, mode);
    if (ourFd < 0) {
        const int e = errno;
        // 原始 trace 若本次 open 本就失败（ret<0），则"复现失败"不算我们的错误
        if (ev.ret < 0) {
            return ExecOutcome{true, false,
                std::format("open 复现原始失败: {} (errno={}, 原始 ret={})",
                            resolved.string(), e, ev.ret)};
        }
        return Error::syscallError(
            std::format("open 失败: {} : {}", resolved.string(), std::strerror(e)),
            "SyscallExecutor::doOpen");
    }

    if (ev.ret >= 0) {
        struct stat st {};
        bool isDir = (::fstat(ourFd, &st) == 0) && S_ISDIR(st.st_mode);
        m_fdTable.registerFd(ev.pid, static_cast<Fd>(ev.ret), ourFd,
                             resolved.string(), isDir);
    } else {
        // 原始失败但我们打开了：为保持 fd 表与原始一致，立即关闭我们的 fd
        ::close(ourFd);
    }
    return ExecOutcome{true, false,
        std::format("open {} → ourFd={} (origFd={})", resolved.string(), ourFd, ev.ret)};
}

Result<ExecOutcome> SyscallExecutor::doIo(const TraceEvent& ev)
{
    // 以 fd 为准：arg1 即原始 fd
    const Fd origFd = static_cast<Fd>(ev.arg1Num);
    const FdEntry* e = m_fdTable.lookup(ev.pid, origFd);
    if (!e) {
        return skipped(std::format("IO 未找到 fd 映射: pid={} fd={}", ev.pid, origFd));
    }

    // 原始 ret = 实际读写字节数。复现"该 fd 上发生 N 字节 IO"的时间序列语义，
    // 不复现数据内容（trace 无负载）。read 用零缓冲，write 写零字节占位。
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
        std::format("{} {} 字节 via ourFd={} ({})", ev.sc, ioLen, e->ourFd, e->path)};
}

Result<ExecOutcome> SyscallExecutor::doClose(const TraceEvent& ev)
{
    const Fd origFd = static_cast<Fd>(ev.arg1Num);
    auto ourFd = m_fdTable.unregisterFd(ev.pid, origFd);
    if (!ourFd) {
        return skipped(std::format("close 未找到 fd 映射: pid={} fd={}", ev.pid, origFd));
    }
    if (::close(*ourFd) < 0) {
        const int e = errno;
        return Error::syscallError(
            std::format("close 失败: ourFd={} : {}", *ourFd, std::strerror(e)),
            "SyscallExecutor::doClose");
    }
    return ExecOutcome{true, false,
        std::format("close ourFd={} (origFd={})", *ourFd, origFd)};
}

Result<ExecOutcome> SyscallExecutor::doStatLike(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    struct stat st {};
    if (::stat(resolved.c_str(), &st) < 0) {
        const int e = errno;
        return skipped(std::format("{} 目标不存在: {} (errno={})", ev.sc, resolved.string(), e));
    }
    return ExecOutcome{true, false,
        std::format("{} → {} (size={})", ev.sc, resolved.string(), st.st_size)};
}

Result<ExecOutcome> SyscallExecutor::doMkdir(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    if (::mkdir(resolved.c_str(), 0755) < 0) {
        const int e = errno;
        if (e == EEXIST) {
            return ExecOutcome{true, false, std::format("mkdir 已存在: {}", resolved.string())};
        }
        return Error::syscallError(
            std::format("mkdir 失败: {} : {}", resolved.string(), std::strerror(e)),
            "SyscallExecutor::doMkdir");
    }
    return ExecOutcome{true, false, std::format("mkdir → {}", resolved.string())};
}

Result<ExecOutcome> SyscallExecutor::doUnlink(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    if (::unlink(resolved.c_str()) < 0) {
        const int e = errno;
        if (e == ENOENT) {
            return skipped(std::format("unlink 目标不存在: {}", resolved.string()));
        }
        return Error::syscallError(
            std::format("unlink 失败: {} : {}", resolved.string(), std::strerror(e)),
            "SyscallExecutor::doUnlink");
    }
    return ExecOutcome{true, false, std::format("unlink → {}", resolved.string())};
}

Result<ExecOutcome> SyscallExecutor::doRename(const TraceEvent& ev)
{
    // rename 需解析源与目标两个路径。PathResolver::resolve 默认取目标侧，
    // 这里单独解析源侧（用 renameSrc）。
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
            return skipped(std::format("rename 源不存在: {}", srcResolved.string()));
        }
        return Error::syscallError(
            std::format("rename 失败: {} -> {} : {}",
                        srcResolved.string(), dstResolved.string(), std::strerror(e)),
            "SyscallExecutor::doRename");
    }
    return ExecOutcome{true, false,
        std::format("rename {} -> {}", srcResolved.string(), dstResolved.string())};
}

#else
// ============================================================================
// Windows 占位实现
//
// 真实 syscall 回放依赖 POSIX 接口（openat/pread64/rename...），在 Windows 上
// 无对应物。Windows 构建仅用于校验解析、排序、fd 表、路径解析等跨平台逻辑
// （DryRunExecutor 路径）。SyscallExecutor 在 Windows 下构造合法但 execute
// 一律返回"平台不支持"，保证整体可编译、可链接。
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
    // 真实 syscall 回放仅在 POSIX 平台可用；Windows 下标记跳过
    (void)ev;
    return ExecOutcome{true, true, "Windows 平台不支持真实 syscall 回放，请用 DryRunExecutor"};
}

#endif  // _WIN32

}  // namespace trace_replay
