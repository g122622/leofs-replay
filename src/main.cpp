// ============================================================================
// trace-replay 入口
//
// 用法:
//   trace_replay <config.json>
//
// 配置文件为 JSON（结构见 README 与 ReplayConfig）。根据 dry_run 字段选择
// DryRunExecutor（仅推演）或 SyscallExecutor（真实执行 syscall）。
// ============================================================================

#include "trace_replay/config/ReplayConfig.hpp"
#include "trace_replay/replay/DryRunExecutor.hpp"
#include "trace_replay/replay/ReplayEngine.hpp"
#include "trace_replay/replay/SyscallExecutor.hpp"

#include <iostream>
#include <memory>
#include <string>

using namespace trace_replay;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "用法: trace_replay <config.json>\n";
        return 2;
    }

    // 加载并校验配置
    auto cfgResult = ReplayConfig::loadFromJson(argv[1]);
    if (!cfgResult.success()) {
        std::cerr << "[error] 配置加载失败: " << cfgResult.error().toString() << '\n';
        return 1;
    }
    auto cfg = std::move(cfgResult).value();

    if (auto v = cfg.validate(); !v.success()) {
        std::cerr << "[error] 配置校验失败: " << v.error().toString() << '\n';
        return 1;
    }

    std::cerr << "[main] events_root=" << cfg.eventsRoot << '\n'
              << "[main] sandbox_root=" << cfg.sandboxRoot << '\n'
              << "[main] dry_run=" << (cfg.dryRun ? "true" : "false") << '\n';

    // 按配置选择执行器
    std::unique_ptr<IExecutor> executor;
    if (cfg.dryRun) {
        executor = std::make_unique<DryRunExecutor>(cfg.sandboxRoot, std::cerr);
    } else {
        executor = std::make_unique<SyscallExecutor>(
            cfg.sandboxRoot, cfg.maxIoBytes, cfg.continueOnError, std::cerr);
    }

    // 构造并运行引擎
    ReplayEngine engine{std::move(cfg), std::move(executor), std::cerr};
    auto result = engine.run();
    if (!result.success()) {
        std::cerr << "[error] 回放中止: " << result.error().toString() << '\n';
        return 1;
    }

    const auto& s = result.value();
    std::cerr << "[main] 完成: total=" << s.totalEvents
              << " processed=" << s.processed
              << " skipped=" << s.skipped
              << " failed=" << s.failed
              << " filtered=" << s.filtered << '\n';
    return 0;
}
