#pragma once

#include <filesystem>
#include <optional>
#include <string>

// Where the player's values of a mod's settings live (docs/modding/reference/mods.md#settings): the section `[Mod.<id>]` of
// openbeach.ini, one key per setting id. Plain ini access, apart from the game so a unit test can use it.
namespace obl::mods {

// The text `file` holds for `key` of `mod`, or nullopt when the file, the section or the key is missing.
std::optional<std::string> ReadModSettingFile(const std::filesystem::path& file, const std::string& mod, const std::string& key);

// Writes it, creating the file or the section; every other line stays. False when the file cannot be written.
bool WriteModSettingFile(const std::filesystem::path& file, const std::string& mod, const std::string& key, const std::string& value);

// The player's `[Keys]` line for an action (docs/modding/reference/actions.md): the key names as written, nullopt when there is none.
std::optional<std::string> ReadKeyBindingFile(const std::filesystem::path& file, const std::string& action);
// Writes it; empty text removes the binding (the action's own keys apply again), "none" unbinds.
bool WriteKeyBindingFile(const std::filesystem::path& file, const std::string& action, const std::string& keys);

} // namespace obl::mods
