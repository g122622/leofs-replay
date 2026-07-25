#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/core/Types.hpp"
#include "trace_replay/model/FdTable.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace trace_replay {

// ============================================================================
// 路径解析器
//
// 把原始 trace 的路径解析为"沙箱内的绝对路径"，并强制做防穿越校验
// （对齐 Cubium 规范 10：验证外部输入、防止路径遍历）。
//
// 解析规则：
//   1. 优先用 canonical_path（rawproc 已把挂载映射到统一 CAPFS 命名空间）；
//      若 mapped=false 或 canonical_path 为空，退化用 path。
//   2. 路径若以 '/' 开头视为绝对路径，直接拼到 sandboxRoot 下；
//      否则视为相对路径，需要 dirfd：
//        * dirfd == AT_FDCWD(-100) → 相对沙箱根
//        * 否则查 fd 表得到该 dirfd 对应的沙箱目录路径，再拼
//   3. 规范化（weakly_canonical）后断言结果仍在 sandboxRoot 之下，否则
//      返回 InvalidArgument 错误，拒绝执行该事件。
// ============================================================================

/// AT_FDCWD 的值（Linux，用于 openat 的 dirfd 表示"相对当前工作目录"）
inline constexpr Fd AT_FDCWD_VALUE = -100;

/**
 * @brief 沙箱路径解析器
 */
class PathResolver {
public:
    explicit PathResolver(std::filesystem::path sandboxRoot);

    /**
     * @brief 解析事件的路径为沙箱内绝对路径
     *
     * @param ev    事件（取其 canonical_path/path、dirfd=arg1）
     * @param fdTbl per-pid fd 表，用于解析相对路径的 dirfd
     * @return 沙箱内绝对路径；失败返回错误（路径穿越等）
     */
    [[nodiscard]] Result<std::filesystem::path> resolve(
        const TraceEvent& ev, const FdTable& fdTbl) const;

    /// 沙箱根
    [[nodiscard]] const std::filesystem::path& sandboxRoot() const noexcept { return m_root; }

private:
    std::filesystem::path m_root;
};

}  // namespace trace_replay
