#include "trace_replay/model/PathResolver.hpp"

#include "trace_replay/core/Assert.hpp"
#include "trace_replay/core/Error.hpp"

#include <filesystem>
#include <system_error>

namespace trace_replay {
namespace {

namespace fs = std::filesystem;

/// 取事件的可信路径：优先 canonical_path，其次 path
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
    // rename 的 path 形如 "src -> dst"，这里取目标侧（dst）作为操作路径；
    // 源/目标分别由 executor 在需要时单独解析。
    if (ev.isRename()) {
        raw = ev.renameDst.empty() ? raw : std::string_view{ev.renameDst};
    }
    if (raw.empty()) {
        return Error::invalidArgument("事件缺少可解析路径", "PathResolver::resolve");
    }

    fs::path target;
    if (raw.front() == '/') {
        // 绝对路径：去掉前导 '/' 后挂到沙箱根下，避免直接访问真实根
        std::string_view rel = raw;
        rel.remove_prefix(1);
        target = m_root / fs::path{std::string{rel}};
    } else {
        // 相对路径：依据 dirfd 决定基目录
        const Fd dirfd = static_cast<Fd>(ev.arg1Num);
        if (dirfd == AT_FDCWD_VALUE) {
            target = m_root / fs::path{std::string{raw}};
        } else {
            const FdEntry* base = fdTbl.lookup(ev.pid, dirfd);
            if (!base) {
                return Error::notFound(
                    "相对路径的 dirfd 未在 fd 表中登记: pid=" + std::to_string(ev.pid) +
                    " dirfd=" + std::to_string(dirfd),
                    "PathResolver::resolve");
            }
            target = fs::path{base->path} / fs::path{std::string{raw}};
        }
    }

    // 规范化并强制防穿越：结果必须在沙箱根之下（或等于根）
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(target, ec);
    if (ec) {
        return Error::invalidArgument(
            "路径规范化失败: " + target.string() + " : " + ec.message(),
            "PathResolver::resolve");
    }

    // 比较 canonical 是否以 m_root 为前缀。weakly_canonical 后两者皆为规范形式。
    auto rootCanon = fs::weakly_canonical(m_root, ec);
    if (ec) {
        return Error::invalidArgument(
            "沙箱根规范化失败: " + m_root.string(),
            "PathResolver::resolve");
    }

    // 用 lexically_relative 判定包含关系：若 canonical 相对 rootCanon 不以 ".." 开头，
    // 则 canonical 在 root 之下。
    auto rel = canonical.lexically_relative(rootCanon);
    bool escapes = false;
    for (const auto& part : rel) {
        if (part == "..") { escapes = true; break; }
    }
    if (escapes) {
        return Error::invalidArgument(
            "路径穿越被拒绝: " + target.string() + " → " + canonical.string(),
            "PathResolver::resolve");
    }

    return canonical;
}

}  // namespace trace_replay
