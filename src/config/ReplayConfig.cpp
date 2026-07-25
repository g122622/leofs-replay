#include "trace_replay/config/ReplayConfig.hpp"

#include "trace_replay/core/Assert.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef TRACE_REPLAY_HAVE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#else
#include <nlohmann/json.hpp>
#endif

namespace trace_replay {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

/// 读取整个文件为字符串。配置文件通常很小，一次读入即可。
[[nodiscard]] Result<std::string> readFile(std::string_view path)
{
    std::ifstream ifs{std::string{path}};
    if (!ifs) {
        return Error::ioError(
            std::string{"无法打开配置文件: "} + std::string{path},
            "ReplayConfig::readFile");
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

/// 安全地从 json 取字符串字段，缺失或类型不符返回默认值
std::string getString(const json& j, const char* key, std::string def = {})
{
    if (j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return def;
}

/// 安全地取整型字段
template <typename Int>
Int getInt(const json& j, const char* key, Int def)
{
    if (j.contains(key) && j[key].is_number_integer()) {
        return j[key].get<Int>();
    }
    return def;
}

double getDouble(const json& j, const char* key, double def)
{
    if (j.contains(key) && j[key].is_number()) {
        return j[key].get<double>();
    }
    return def;
}

bool getBool(const json& j, const char* key, bool def)
{
    if (j.contains(key) && j[key].is_boolean()) {
        return j[key].get<bool>();
    }
    return def;
}

PaceMode parsePaceMode(std::string_view s)
{
    if (s == "real")   return PaceMode::Real;
    if (s == "scaled") return PaceMode::Scaled;
    return PaceMode::Fast;   // "fast" 或未知都退化为最快
}

Side parseSide(std::string_view s)
{
    if (s == "source") return Side::Source;
    if (s == "target") return Side::Target;
    return Side::Other;      // "other"/"all" 表示不过滤
}

}  // namespace

Result<ReplayConfig> ReplayConfig::loadFromJson(std::string_view path)
{
    TR_TRY(content, readFile(path));

    ReplayConfig cfg;
    json j;
    try {
        j = json::parse(content);
    } catch (const json::parse_error& e) {
        return Error::invalidFormat(
            std::string{"JSON 解析失败: "} + e.what(),
            "ReplayConfig::loadFromJson");
    }

    // 输入
    cfg.eventsRoot   = fs::path{getString(j, "events_root")};
    cfg.bucketWidth  = getInt(j, "bucket_width", 6);
    cfg.bucketMin    = getInt(j, "bucket_min", 0);
    cfg.bucketMax    = getInt(j, "bucket_max", -1);

    // 沙箱
    cfg.sandboxRoot  = fs::path{getString(j, "sandbox_root")};

    // 节拍
    cfg.paceMode     = parsePaceMode(getString(j, "pace_mode", "fast"));
    cfg.speed        = getDouble(j, "speed", 1.0);

    // 过滤
    cfg.sideFilter   = parseSide(getString(j, "side_filter", "all"));
    cfg.skipUnparsed = getBool(j, "skip_unparsed", true);

    // 行为
    cfg.dryRun         = getBool(j, "dry_run", false);
    cfg.maxIoBytes     = getInt(j, "max_io_bytes", 1 << 20);
    cfg.continueOnError = getBool(j, "continue_on_error", true);

    // pid 过滤列表
    cfg.pidFilter.clear();
    if (j.contains("pid_filter") && j["pid_filter"].is_array()) {
        for (const auto& v : j["pid_filter"]) {
            if (v.is_number_integer()) {
                cfg.pidFilter.push_back(v.get<i64>());
            }
        }
    }

    return cfg;
}

Result<void> ReplayConfig::validate()
{
    if (eventsRoot.empty()) {
        return Error::invalidArgument("events_root 不能为空", "ReplayConfig::validate");
    }
    if (sandboxRoot.empty()) {
        return Error::invalidArgument("sandbox_root 不能为空", "ReplayConfig::validate");
    }

    // 规范化为绝对路径，便于后续路径穿越校验
    std::error_code ec;
    auto absEvents  = fs::absolute(eventsRoot, ec);
    auto absSandbox = fs::absolute(sandboxRoot, ec);
    if (ec) {
        return Error::invalidArgument(
            std::string{"路径规范化失败: "} + ec.message(),
            "ReplayConfig::validate");
    }
    eventsRoot   = fs::weakly_canonical(absEvents, ec);
    sandboxRoot  = fs::weakly_canonical(absSandbox, ec);

    if (!fs::exists(eventsRoot)) {
        return Error::notFound(
            "events_root 不存在: " + eventsRoot.string(),
            "ReplayConfig::validate");
    }
    // sandboxRoot 若不存在则尝试创建（首次回放需要落盘）
    if (!fs::exists(sandboxRoot)) {
        fs::create_directories(sandboxRoot, ec);
        if (ec) {
            return Error::ioError(
                "无法创建 sandbox_root: " + sandboxRoot.string() + " : " + ec.message(),
                "ReplayConfig::validate");
        }
    }

    if (paceMode == PaceMode::Scaled && speed <= 0.0) {
        return Error::invalidArgument("scaled 模式下 speed 必须为正", "ReplayConfig::validate");
    }
    if (bucketWidth < 1) {
        return Error::invalidArgument("bucket_width 必须 >= 1", "ReplayConfig::validate");
    }

    return Result<void>::ok();
}

}  // namespace trace_replay
