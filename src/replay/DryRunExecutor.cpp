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
    // dry-run uniformly prints an event summary.
    // ret/err/arg1/arg2 are [decimal] strings in rawproc's output: openat's
    // ret=fd, arg1=flags (e.g. 524288=O_CLOEXEC); read/write's ret=actual byte
    // count, arg1=requested byte count; close's ret=0. All printed in decimal
    // here for easy line-by-line comparison with the original trace.
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
    return skipped("non open/io/close/meta operation, skipped");
}

Result<ExecOutcome> DryRunExecutor::doOpen(const TraceEvent& ev)
{
    // dry-run: resolve the path but do not really open; register a virtual
    // ourFd (simulated via the member counter). The original trace's ret is the
    // fd returned by openat (a small integer on success); on failure ret<0 and
    // nothing is registered.
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    Fd ourFd = m_nextDryFd++;
    if (ev.ret >= 0) {
        // Register using bestLookupPath (canonical_path preferred) as the
        // reverse-lookup key, consistent with IO/close.
        m_fdTable.registerFd(ev.pid, static_cast<Fd>(ev.ret), ourFd,
                             std::string{bestLookupPath(ev)}, /*isDir=*/false);
    }
    return ExecOutcome{true, false,
        std::format("open {} -> ourFd={} (origFd={})", resolved.string(), ourFd, ev.ret)};
}

Result<ExecOutcome> DryRunExecutor::doIo(const TraceEvent& ev)
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
    return ExecOutcome{true, false,
        std::format("{} {} bytes via ourFd={} ({})", ev.sc, ev.ret, e->ourFd, e->path)};
}

Result<ExecOutcome> DryRunExecutor::doClose(const TraceEvent& ev)
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
    return ExecOutcome{true, false,
        std::format("close ourFd={} (path=\"{}\")", *ourFd, lookupPath)};
}

Result<ExecOutcome> DryRunExecutor::doMeta(const TraceEvent& ev)
{
    TR_TRY(resolved, m_resolver.resolve(ev, m_fdTable));
    return ExecOutcome{true, false,
        std::format("{} -> {}", ev.sc, resolved.string())};
}

}  // namespace trace_replay
