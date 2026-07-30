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

/// Read the entire file as a string. Config files are usually small; one read
/// suffices.
[[nodiscard]] Result<std::string> readFile(std::string_view path)
{
    std::ifstream ifs{std::string{path}};
    if (!ifs) {
        return Error::ioError(
            std::string{"cannot open config file: "} + std::string{path},
            "ReplayConfig::readFile");
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

/// Safely read a string field from json; returns the default if missing or the
/// type mismatches.
std::string getString(const json& j, const char* key, std::string def = {})
{
    if (j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return def;
}

/// Convert a UTF-8 std::string returned by nlohmann json into an fs::path.
///
/// Key point: on Windows, fs::path(std::string) interprets the bytes using the
/// system ACP (CP_ACP), so non-ASCII paths (e.g. a directory containing CJK
/// characters such as "下载") get corrupted, causing fs::exists/Open to fail.
/// nlohmann json strings are UTF-8, so we go through a u8string here to let
/// fs::path decode as UTF-8, correctly handling arbitrary Unicode paths
/// cross-platform.
fs::path utf8ToPath(std::string_view s)
{
#if defined(_WIN32)
    // Windows: fs::path(u8string) decodes as UTF-8 into internal UTF-16 storage
    return fs::path{std::u8string{s.begin(), s.end()}};
#else
    // POSIX: native char is UTF-8; construct directly
    return fs::path{std::string{s}};
#endif
}

/// Safely read an integer field
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
    return PaceMode::Fast;   // "fast" or unknown both fall back to fastest
}

Side parseSide(std::string_view s)
{
    if (s == "source") return Side::Source;
    if (s == "target") return Side::Target;
    return Side::Other;      // "other"/"all" means no filtering
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
            std::string{"JSON parse failed: "} + e.what(),
            "ReplayConfig::loadFromJson");
    }

    // Input
    cfg.eventsRoot   = utf8ToPath(getString(j, "events_root"));
    cfg.bucketWidth  = getInt(j, "bucket_width", 6);
    cfg.bucketMin    = getInt(j, "bucket_min", 0);
    cfg.bucketMax    = getInt(j, "bucket_max", -1);

    // Sandbox
    cfg.sandboxRoot  = utf8ToPath(getString(j, "sandbox_root"));

    // Pacing
    cfg.paceMode     = parsePaceMode(getString(j, "pace_mode", "fast"));
    cfg.speed        = getDouble(j, "speed", 1.0);

    // Filtering
    cfg.sideFilter   = parseSide(getString(j, "side_filter", "all"));
    cfg.skipUnparsed = getBool(j, "skip_unparsed", true);

    // Behavior
    cfg.dryRun         = getBool(j, "dry_run", false);
    cfg.maxIoBytes     = getInt(j, "max_io_bytes", 1 << 20);
    cfg.continueOnError = getBool(j, "continue_on_error", true);
    cfg.maxEvents      = static_cast<u64>(getInt<i64>(j, "max_events", 0));

    // pid filter list
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
        return Error::invalidArgument("events_root must not be empty", "ReplayConfig::validate");
    }
    if (sandboxRoot.empty()) {
        return Error::invalidArgument("sandbox_root must not be empty", "ReplayConfig::validate");
    }

    // Normalize to absolute paths for later path-traversal checks
    std::error_code ec;
    auto absEvents  = fs::absolute(eventsRoot, ec);
    auto absSandbox = fs::absolute(sandboxRoot, ec);
    if (ec) {
        return Error::invalidArgument(
            std::string{"path normalization failed: "} + ec.message(),
            "ReplayConfig::validate");
    }
    eventsRoot   = fs::weakly_canonical(absEvents, ec);
    sandboxRoot  = fs::weakly_canonical(absSandbox, ec);

    if (!fs::exists(eventsRoot)) {
        return Error::notFound(
            "events_root does not exist: " + eventsRoot.string(),
            "ReplayConfig::validate");
    }
    // If sandboxRoot does not exist, try to create it (first-time replay needs
    // to persist to disk)
    if (!fs::exists(sandboxRoot)) {
        fs::create_directories(sandboxRoot, ec);
        if (ec) {
            return Error::ioError(
                "cannot create sandbox_root: " + sandboxRoot.string() + " : " + ec.message(),
                "ReplayConfig::validate");
        }
    }

    if (paceMode == PaceMode::Scaled && speed <= 0.0) {
        return Error::invalidArgument("speed must be positive in scaled mode", "ReplayConfig::validate");
    }
    if (bucketWidth < 1) {
        return Error::invalidArgument("bucket_width must be >= 1", "ReplayConfig::validate");
    }

    return Result<void>::ok();
}

}  // namespace trace_replay
