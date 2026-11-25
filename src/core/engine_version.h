#pragma once

#include <charconv>
#include <optional>
#include <string_view>

// The game's own version and the `MAJOR.MINOR.PATCH` versions mods and catalogs compare against it (`engine_min` in mod.json,
// `engine_min`/`engine_max` in catalogs). Header-only and portable; nothing here reaches the simulation.
namespace obl {

#ifdef OBL_ENGINE_VERSION
inline constexpr std::string_view kEngineVersion = OBL_ENGINE_VERSION;
#else
inline constexpr std::string_view kEngineVersion = "1.13.0";
#endif

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// Exactly three dot-separated numbers without leading zeros; anything else (pre-release tags included) is refused.
inline bool ParseVersion(std::string_view text, SemVer& out)
{
    int parts[3] = {0, 0, 0};
    size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t dot = text.find('.', start);
        if ((i < 2 && dot == std::string_view::npos) || (i == 2 && dot != std::string_view::npos)) return false;
        const std::string_view seg = text.substr(start, dot == std::string_view::npos ? dot : dot - start);
        if (seg.empty() || (seg.size() > 1 && seg[0] == '0')) return false;
        int val = 0;
        const auto res = std::from_chars(seg.data(), seg.data() + seg.size(), val);
        if (res.ec != std::errc{} || res.ptr != seg.data() + seg.size() || val < 0) return false;
        parts[i] = val;
        start = dot + 1;
    }
    out = {parts[0], parts[1], parts[2]};
    return true;
}

inline int CompareVersion(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

inline bool InVersionRange(const SemVer& engine, const std::optional<SemVer>& minVersion, const std::optional<SemVer>& maxVersion)
{
    if (minVersion && CompareVersion(engine, *minVersion) < 0) return false;
    if (maxVersion && CompareVersion(engine, *maxVersion) > 0) return false;
    return true;
}

// The running game's version; a build whose version text does not parse counts as the newest possible, so no mod is refused.
inline SemVer EngineVersion()
{
    SemVer v{};
    if (!ParseVersion(kEngineVersion, v)) return {999999, 0, 0};
    return v;
}

} // namespace obl
