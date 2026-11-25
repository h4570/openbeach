#pragma once

// Merged object-script vocabulary (states and messages): the pack's `scripts/object_vocabulary.json` plus any active
// mod's `scripts/object_vocabulary.patch.json` (`obl.object_script_vocabulary_patch` v1). Mod names receive sequential
// ids after the pack's maximum id, in mod load order and sorted by name within each mod; any collision with the pack or
// an earlier mod is refused naming both. Shared by the object script loader and obl.save (so saves write and read mod
// state names). Pure logic: no game memory, no Windows APIs.

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::object_vocabulary {

inline constexpr const char* kBaseFormat = "obl.object_script_vocabulary";
inline constexpr int kBaseVersion = 1;
inline constexpr const char* kPatchFormat = "obl.object_script_vocabulary_patch";
inline constexpr int kPatchVersion = 1;
inline constexpr const char* kVocabularyPath = "scripts/object_vocabulary.json";
inline constexpr const char* kPatchPath = "scripts/object_vocabulary.patch.json";

struct Entry {
    int id = 0;
    std::string name;
};

// Parses a pack's `scripts/object_vocabulary.json`. Returns false and sets `error` (clearing outputs) on failure.
bool ParseBase(std::string_view json, std::vector<Entry>& states, std::vector<Entry>& messages, std::string& error);

// Parses a mod's `scripts/object_vocabulary.patch.json`. State names must start with `STATE_`, message names with `MT_`.
// Rejects duplicates within the patch. Returns false and sets `error` (clearing outputs) on failure.
bool ParsePatch(std::string_view json, std::vector<std::string>& states, std::vector<std::string>& messages, std::string& error);

// Loads the pack's vocabulary and merges every active mod's `scripts/object_vocabulary.patch.json` in load order.
// Returns false and sets `error` (naming the mod and any colliding pack/mod) on failure.
bool LoadMerged(const std::filesystem::path& packDirectory, std::span<const std::string> mods, std::vector<Entry>& states,
        std::vector<Entry>& messages, std::string& error);

} // namespace obl::object_vocabulary
