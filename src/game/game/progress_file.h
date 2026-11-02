#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

// The campaign progress of the user, kept outside the packs as `progress.json` in the game directory:
// {"format": "obl.progress", "version": 1, "scenarios": [12 flag values]}. The flags are game_core.h's (1 locked, 2 unlocked,
// 4 always available, 8 completed). The 48-byte `sp.bin` of the installed game is only read, once, when `progress.json` is absent.
namespace obl::progress_file {

inline constexpr const char* kFileName = "progress.json";
inline constexpr const char* kSeedFileName = "sp.bin";
inline constexpr size_t kSeedSize = 48;

inline std::string ToJson(const int32_t (&progress)[12])
{
    nlohmann::json document = {{"format", "obl.progress"}, {"version", 1}, {"scenarios", nlohmann::json::array()}};
    for (const int32_t value : progress) document["scenarios"].push_back(value);
    return document.dump(2) + "\n";
}

// False (and `progress` untouched) for anything but a version-1 document with 12 flag values of 0 to 15.
inline bool FromJson(std::string_view text, int32_t (&progress)[12])
{
    const nlohmann::json document = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object() || document.value("format", "") != "obl.progress" || document.value("version", 0) != 1) return false;
    const auto found = document.find("scenarios");
    if (found == document.end() || !found->is_array() || found->size() != 12) return false;
    int32_t parsed[12];
    for (size_t i = 0; i < 12; ++i) {
        const nlohmann::json& value = (*found)[i];
        if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<int64_t>() > 15) return false;
        parsed[i] = value.get<int32_t>();
    }
    std::memcpy(progress, parsed, sizeof(parsed));
    return true;
}

// The installed game's `sp.bin`: 12 little-endian int32 values. Same range rule as the JSON.
inline bool FromSeedFile(std::string_view bytes, int32_t (&progress)[12])
{
    if (bytes.size() != kSeedSize) return false;
    int32_t parsed[12];
    std::memcpy(parsed, bytes.data(), kSeedSize);
    for (const int32_t value : parsed)
        if (value < 0 || value > 15) return false;
    std::memcpy(progress, parsed, sizeof(parsed));
    return true;
}

} // namespace obl::progress_file
