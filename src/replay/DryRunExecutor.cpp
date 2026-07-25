#include "trace_replay/replay/DryRunExecutor.hpp"

#include "trace_replay/core/Error.hpp"
#include "trace_replay/core/SyscallClassify.hpp"

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
    // dry-run 统一打印事件摘要
    m_log << std::format("[dry] ts={:.6f} pid={} {} ret={} err={} arg1=0x{:x} arg2=0x{:x} path=\"{}\"\n",
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
    // dry-run：解析路径但不真实打开，登记一个虚拟 ourFd（用成员计数器模拟）
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    Fd ourFd = m_nextDryFd++;
    // 原始 trace 的 ret 即 openat 返回的 fd（成功时）；失败时 ret<0 不登记
    if (ev.ret >= 0) {
        m_fdTable.registerFd(ev.pid, static_cast<Fd>(ev.ret), ourFd,
                             resolved.string(), /*isDir=*/false);
    }
    return ExecOutcome{true, false,
        std::format("open {} → ourFd={} (origFd={})", resolved.string(), ourFd, ev.ret)};
}

Result<ExecOutcome> DryRunExecutor::doIo(const TraceEvent& ev)
{
    // 以 fd 为准：arg1 即原始 fd
    const Fd origFd = static_cast<Fd>(ev.arg1Num);
    const FdEntry* e = m_fdTable.lookup(ev.pid, origFd);
    if (!e) {
        return skipped(std::format("IO 未找到 fd 映射: pid={} fd={}", ev.pid, origFd));
    }
    return ExecOutcome{true, false,
        std::format("{} {} 字节 via ourFd={} ({})", ev.sc, ev.ret, e->ourFd, e->path)};
}

Result<ExecOutcome> DryRunExecutor::doClose(const TraceEvent& ev)
{
    const Fd origFd = static_cast<Fd>(ev.arg1Num);
    auto ourFd = m_fdTable.unregisterFd(ev.pid, origFd);
    if (!ourFd) {
        return skipped(std::format("close 未找到 fd 映射: pid={} fd={}", ev.pid, origFd));
    }
    return ExecOutcome{true, false,
        std::format("close ourFd={} (origFd={})", *ourFd, origFd)};
}

Result<ExecOutcome> DryRunExecutor::doMeta(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    return ExecOutcome{true, false,
        std::format("{} → {}", ev.sc, resolved.string())};
}

}  // namespace trace_replay
