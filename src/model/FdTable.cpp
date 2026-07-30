#include "trace_replay/model/FdTable.hpp"

#include "trace_replay/core/Assert.hpp"

namespace trace_replay {

void FdTable::registerFd(i64 pid, Fd origFd, Fd ourFd,
                         std::string path, bool isDir)
{
    TR_ASSERT(origFd >= 0);
    TR_ASSERT(ourFd >= 0);
    auto& pp = m_table[pid];
    // If this origFd already exists (fd reuse), first remove the old path's
    // pointer from the path index.
    auto it = pp.byFd.find(origFd);
    if (it != pp.byFd.end() && !it->second.path.empty()) {
        pp.byPath.erase(it->second.path);
    }
    pp.byFd[origFd] = FdEntry{ourFd, path, isDir};
    if (!path.empty()) {
        pp.byPath[path] = origFd;
    }
}

std::optional<Fd> FdTable::unregisterFd(i64 pid, Fd origFd)
{
    auto pit = m_table.find(pid);
    if (pit == m_table.end()) {
        return std::nullopt;
    }
    auto& pp = pit->second;
    auto fit = pp.byFd.find(origFd);
    if (fit == pp.byFd.end()) {
        return std::nullopt;
    }
    const Fd ourFd = fit->second.ourFd;
    if (!fit->second.path.empty()) {
        pp.byPath.erase(fit->second.path);
    }
    pp.byFd.erase(fit);
    if (pp.byFd.empty()) {
        m_table.erase(pit);
    }
    return ourFd;
}

std::optional<Fd> FdTable::unregisterByPath(i64 pid, std::string_view path)
{
    auto pit = m_table.find(pid);
    if (pit == m_table.end()) {
        return std::nullopt;
    }
    auto& pp = pit->second;
    auto spit = pp.byPath.find(std::string{path});
    if (spit == pp.byPath.end()) {
        return std::nullopt;
    }
    const Fd origFd = spit->second;
    pp.byPath.erase(spit);
    auto fit = pp.byFd.find(origFd);
    if (fit == pp.byFd.end()) {
        return std::nullopt;
    }
    const Fd ourFd = fit->second.ourFd;
    pp.byFd.erase(fit);
    if (pp.byFd.empty()) {
        m_table.erase(pit);
    }
    return ourFd;
}

const FdEntry* FdTable::lookup(i64 pid, Fd origFd) const
{
    auto pit = m_table.find(pid);
    if (pit == m_table.end()) {
        return nullptr;
    }
    auto fit = pit->second.byFd.find(origFd);
    if (fit == pit->second.byFd.end()) {
        return nullptr;
    }
    return &fit->second;
}

const FdEntry* FdTable::lookupByPath(i64 pid, std::string_view path) const
{
    auto pit = m_table.find(pid);
    if (pit == m_table.end()) {
        return nullptr;
    }
    auto spit = pit->second.byPath.find(std::string{path});
    if (spit == pit->second.byPath.end()) {
        return nullptr;
    }
    auto fit = pit->second.byFd.find(spit->second);
    if (fit == pit->second.byFd.end()) {
        return nullptr;
    }
    return &fit->second;
}

void FdTable::closePid(i64 pid)
{
    m_table.erase(pid);
}

size_t FdTable::size() const noexcept
{
    size_t n = 0;
    for (const auto& [_, pp] : m_table) {
        n += pp.byFd.size();
    }
    return n;
}

}  // namespace trace_replay
