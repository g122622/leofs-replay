#include "trace_replay/model/FdTable.hpp"

#include "trace_replay/core/Assert.hpp"

namespace trace_replay {

void FdTable::registerFd(i64 pid, Fd origFd, Fd ourFd,
                         std::string path, bool isDir)
{
    TR_ASSERT(origFd >= 0);
    TR_ASSERT(ourFd >= 0);
    m_table[pid][origFd] = FdEntry{ourFd, std::move(path), isDir};
}

std::optional<Fd> FdTable::unregisterFd(i64 pid, Fd origFd)
{
    auto pit = m_table.find(pid);
    if (pit == m_table.end()) {
        return std::nullopt;
    }
    auto fit = pit->second.find(origFd);
    if (fit == pit->second.end()) {
        return std::nullopt;
    }
    const Fd ourFd = fit->second.ourFd;
    pit->second.erase(fit);
    // pid 的表空了顺手清理，避免 m_table 无限增长
    if (pit->second.empty()) {
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
    auto fit = pit->second.find(origFd);
    if (fit == pit->second.end()) {
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
    for (const auto& [_, inner] : m_table) {
        n += inner.size();
    }
    return n;
}

}  // namespace trace_replay
