// ============================================================================
// trace-replay entry point
//
// Usage:
//   trace_replay <config.json>
//
// The config file is JSON (structure in README and ReplayConfig). Based on the
// dry_run field, selects DryRunExecutor (simulate only) or SyscallExecutor
// (real syscall execution).
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
        std::cerr << "Usage: trace_replay <config.json>\n";
        return 2;
    }

    // Load and validate the config
    auto cfgResult = ReplayConfig::loadFromJson(argv[1]);
    if (!cfgResult.success()) {
        std::cerr << "[error] config load failed: " << cfgResult.error().toString() << '\n';
        return 1;
    }
    auto cfg = std::move(cfgResult).value();

    if (auto v = cfg.validate(); !v.success()) {
        std::cerr << "[error] config validation failed: " << v.error().toString() << '\n';
        return 1;
    }

    std::cerr << "[main] events_root=" << cfg.eventsRoot << '\n'
              << "[main] sandbox_root=" << cfg.sandboxRoot << '\n'
              << "[main] dry_run=" << (cfg.dryRun ? "true" : "false") << '\n';

    // Select the executor per config
    std::unique_ptr<IExecutor> executor;
    if (cfg.dryRun) {
        executor = std::make_unique<DryRunExecutor>(cfg.sandboxRoot, std::cerr);
    } else {
        executor = std::make_unique<SyscallExecutor>(
            cfg.sandboxRoot, cfg.maxIoBytes, cfg.continueOnError, std::cerr);
    }

    // Build and run the engine
    ReplayEngine engine{std::move(cfg), std::move(executor), std::cerr};
    auto result = engine.run();
    if (!result.success()) {
        std::cerr << "[error] replay aborted: " << result.error().toString() << '\n';
        return 1;
    }

    const auto& s = result.value();
    std::cerr << "[main] done: total=" << s.totalEvents
              << " processed=" << s.processed
              << " skipped=" << s.skipped
              << " failed=" << s.failed
              << " filtered=" << s.filtered << '\n';
    return 0;
}
