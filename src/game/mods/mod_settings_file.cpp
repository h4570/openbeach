#include "game/mods/mod_settings_file.h"

#include "core/ini_file.h"

namespace obl::mods {

namespace {

// ReadString cannot tell a missing key from an empty one; this value (a control character no file holds) can.
constexpr const char* kMissing = "\x01missing";
constexpr size_t kMaxText = 256;

} // namespace

std::optional<std::string> ReadModSettingFile(const std::filesystem::path& file, const std::string& mod, const std::string& key)
{
    std::string value = ini::ReadString(file, "Mod." + mod, key, kMissing, kMaxText);
    if (value == kMissing) return std::nullopt;
    return value;
}

bool WriteModSettingFile(const std::filesystem::path& file, const std::string& mod, const std::string& key, const std::string& value)
{
    return ini::WriteString(file, "Mod." + mod, key, value);
}

} // namespace obl::mods
