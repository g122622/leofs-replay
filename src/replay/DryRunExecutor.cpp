#include "trace_replay/replay/DryRunExecutor.hpp"

#include "trace_replay/core/Error.hpp"
#include "trace_replay/core/SyscallClassify.hpp"
#include "trace_replay/core/TraceEventUtil.hpp"

#include <format>
#include <ostream>

namespace trace_replay {
namespace {

ExecOutcome skipped(std::string why)
{
    return ExecOutcome{true, true, std::move(why)};
}

}  // namespace

DryRunExecutor::DryRunExecutor(std::filesystem::path sandboxRoot, std::ostream& log)
    : m_resolver(std::move(sandboxRoot))
    , m_log(log) {}

Result<ExecOutcome> DryRunExecutor::execute(const TraceEvent& ev)
{
    // dry-run 统一打印事件摘要。
    // ret/err/arg1/arg2 在 rawproc 输出中是【十进制】字符串：openat 的 ret=fd、
    // arg1=flags（如 524288=O_CLOEXEC）；read/write 的 ret=实际字节数、arg1=请求字节数；
    // close 的 ret=0。这里都按十进制打印，便于和原始 trace 逐行比对。
    m_log << std::format("[dry] ts={:.6f} pid={} {} ret={} err={} arg1={} arg2={} path=\"{}\"\n",
                         ev.machineTs, ev.pid, ev.sc, ev.ret, ev.err,
                         ev.arg1Num, ev.arg2Num, ev.path);

    if (isOpenOp(ev.sc)) {
        return doOpen(ev);
    }
    if (ev.sc == "close") {
        return doClose(ev);
    }
    if (ev.isIo()) {
        return doIo(ev);
    }
    if (ev.isMeta()) {
        return doMeta(ev);
    }
    return skipped("非 open/io/close/meta 操作，跳过");
}

Result<ExecOutcome> DryRunExecutor::doOpen(const TraceEvent& ev)
{
    // dry-run：解析路径但不真实打开，登记一个虚拟 ourFd（用成员计数器模拟）。
    // 原始 trace 的 ret 即 openat 返回的 fd（成功时小整数）；失败时 ret<0 不登记。
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    Fd ourFd = m_nextDryFd++;
    if (ev.ret >= 0) {
        // 登记时用 bestLookupPath（canonical_path 优先）作为反查键，与 IO/close 一致
        m_fdTable.registerFd(ev.pid, static_cast<Fd>(ev.ret), ourFd,
                             std::string{bestLookupPath(ev)}, /*isDir=*/false);
    }
    return ExecOutcome{true, false,
        std::format("open {} → ourFd={} (origFd={})", resolved.string(), ourFd, ev.ret)};
}

Result<ExecOutcome> DryRunExecutor::doIo(const TraceEvent& ev)
{
    // fd 跟踪改为 path 反查：read/write 的 arg1 是字节数 count，不是 fd。
    // 优先用 bestLookupPath（canonical_path 优先，退化 path）在该 pid 下反查 ourFd；
    // 若 path 为 "/[unknown, fd=N]" 形态，提取 N 走 fd 直查回退。
    const std::string_view lookupPath = bestLookupPath(ev);
    const FdEntry* e = nullptr;
    if (auto fd = extractUnknownFd(lookupPath)) {
        e = m_fdTable.lookup(ev.pid, *fd);
    } else {
        e = m_fdTable.lookupByPath(ev.pid, lookupPath);
    }
    if (!e) {
        return skipped(std::format("IO 未找到 fd 映射: pid={} path=\"{}\"", ev.pid, lookupPath));
    }
    return ExecOutcome{true, false,
        std::format("{} {} 字节 via ourFd={} ({})", ev.sc, ev.ret, e->ourFd, e->path)};
}

Result<ExecOutcome> DryRunExecutor::doClose(const TraceEvent& ev)
{
    // close 的 arg1/ret 均为 0，fd 信息不在 arg，同样靠 path 反查。
    const std::string_view lookupPath = bestLookupPath(ev);
    std::optional<Fd> ourFd;
    if (auto fd = extractUnknownFd(lookupPath)) {
        ourFd = m_fdTable.unregisterFd(ev.pid, *fd);
    } else {
        ourFd = m_fdTable.unregisterByPath(ev.pid, lookupPath);
    }
    if (!ourFd) {
        return skipped(std::format("close 未找到 fd 映射: pid={} path=\"{}\"", ev.pid, lookupPath));
    }
    return ExecOutcome{true, false,
        std::format("close ourFd={} (path=\"{}\")", *ourFd, lookupPath)};
}

Result<ExecOutcome> DryRunExecutor::doMeta(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    return ExecOutcome{true, false,
        std::format("{} → {}", ev.sc, resolved.string())};
}

}  // namespace trace_replay
