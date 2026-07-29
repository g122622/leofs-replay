#pragma once

#include "trace_replay/core/Result.hpp"
#include "trace_replay/core/TraceEvent.hpp"
#include "trace_replay/core/Types.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trace_replay {

// ============================================================================
// 回放节拍模式
//
//   Fast      —— 不 sleep，尽快按时间序处理（默认，关注正确性与吞吐）
//   Real      —— 按原始事件间墙钟间隔 sleep，1:1 还原真实时间分布
//   Scaled    —— 按 speed 比例缩放间隔（speed>1 加速，<1 减速）
// ============================================================================

enum class PaceMode : u8 {
    Fast,
    Real,
    Scaled,
};

/**
 * @brief replay 运行配置
 *
 * 由 JSON 配置文件构造（见 ReplayConfig::loadFromJson）。所有路径均相对
 * 沙箱根 sandboxRoot 解析并做防穿越校验。字段命名对齐 JSON key（snake_case）。
 */
struct ReplayConfig {
    // —— 输入 ——
    /// rawproc 产出根目录，其下应有 events_tsorted/bucket=xxxxxx
    std::filesystem::path eventsRoot;
    /// 桶目录名宽度（默认 6，对齐 rawproc width=max(6, ...)）
    int bucketWidth {6};
    /// 只回放指定范围内的桶（闭区间），留空表示全部
    long bucketMin {0};
    long bucketMax {-1};   // <0 表示无上限

    // —— 沙箱 ——
    /// 真实 syscall 的根目录，所有 replay 路径都限制在其下
    std::filesystem::path sandboxRoot;

    // —— 节拍 ——
    PaceMode paceMode {PaceMode::Fast};
    /// Scaled 模式下的倍速（2.0 = 两倍速回放）
    double speed {1.0};

    // —— 过滤 ——
    /// 只回放这些 pid 的事件（空表示全部）
    std::vector<i64> pidFilter;
    /// 只回放 source 侧 / target 侧 / 全部
    Side sideFilter {Side::Other};   // Other 表示不过滤
    /// 是否跳过 _unparsed / null_ts 桶
    bool skipUnparsed {true};

    // —— 行为 ——
    /// 是否只做 dry-run（不执行真实 syscall，仅打印/统计）
    bool dryRun {false};
    /// read/write 时单次最大字节数（避免一次性读过大的 trace offset）
    i64 maxIoBytes {1 << 20};   // 默认 1 MiB
    /// 遇到 syscall 失败时是否继续（true=跳过并计数，false=中止）
    bool continueOnError {true};
    /// 回放事件上限（>0 时处理到该数量即停，便于对大文件做有界 dry-run；0=不限）
    u64 maxEvents {0};

    /**
     * @brief 从 JSON 配置文件加载
     *
     * 期望的 JSON 结构示例见 README。缺失字段使用结构体默认值。
     *
     * @param path JSON 配置文件路径
     * @return Result<ReplayConfig>
     */
    [[nodiscard]] static Result<ReplayConfig> loadFromJson(std::string_view path);

    /**
     * @brief 校验配置合法性并规范化路径
     *
     * 将相对路径转为绝对、检查 sandboxRoot 存在、eventsRoot 存在等。
     */
    [[nodiscard]] Result<void> validate();
};

}  // namespace trace_replay
