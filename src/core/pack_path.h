#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The one resolver for logical pack paths (`maps/1/map.json`, `gui/layouts.json`, ...). Layers, first match wins:
//   1. `<game dir>/mods/<id>/<logical>`  the active mods, the last of the list first (docs/modding/reference/mods.md),
//   2. `<game dir>/mod/<logical>`        the legacy user layer,
//   3. `<game dir>/packs/<id>/<logical>` the selected pack (`base`, the converted one, unless OBL_PACK names another).
// `pack` is the pack directory (`<game dir>/packs/<id>`); the other layers hang off `<pack>/../..`. Every loader of pack data
// builds its file names through this (docs/build/game-data.md). Pure logic: no Windows APIs.
namespace obl::pack_path {

// The pack a game runs on when OBL_PACK is not set: the converted pack.
inline constexpr const char* kDefaultPack = "base";

// `packs/<id>` for a pack id (lowercase letters, digits, `_` and `-`, so it can only name a directory under `packs/`);
// nullopt for anything else.
std::optional<std::filesystem::path> PackDirectory(std::string_view id);

// The id the game runs on: OBL_PACK when set, otherwise kDefaultPack. Not validated; see Selected.
std::string SelectedId();

// The selected pack directory, relative to the game directory (the game's and obl-sim's working directory). An invalid
// OBL_PACK yields an empty path; the start-up check (CheckSelected) stops the game before any loader asks.
std::filesystem::path Selected();

// False with `error` when OBL_PACK is not a valid pack id, or names a pack without `pack.json` under `gameDirectory` (the
// default pack is not checked here: its loaders report a missing file themselves).
bool CheckSelected(const std::filesystem::path& gameDirectory, std::string& error);

// The legacy mod layer directory (`<game dir>/mod`) belonging to `pack`.
std::filesystem::path ModLayer(const std::filesystem::path& pack);

// The game directory `pack` lives in.
std::filesystem::path GameDirectory(const std::filesystem::path& pack);

// `<game dir>/mods/<id>`; nullopt when `id` is not a valid id (the rules of PackDirectory).
std::optional<std::filesystem::path> ModDirectory(const std::filesystem::path& gameDirectory, std::string_view id);

// `<game dir>/mod-cache/<id>`: recipe-derived assets cached next to the mod; nullopt when `id` is not a valid id.
std::optional<std::filesystem::path> ModCacheDirectory(const std::filesystem::path& gameDirectory, std::string_view id);

// A checker source directory or an immutable developer-mode copy. An empty path clears the override.
void SetModDirectoryOverride(std::string id, std::filesystem::path directory);

// The ids in a comma separated list (`a,b`), blanks around an id dropped, empty items skipped. Not validated.
std::vector<std::string> ParseModList(std::string_view list);

// The mods the game runs with, first to last in load order. The start-up sets them once (the `[Mods] Active` setting, or
// OBL_MODS when it is set) and stops the game when the check (mod_pack::CheckActive) refuses them; the default is none.
void SetActiveMods(std::vector<std::string> ids);
const std::vector<std::string>& ActiveMods();

// Every layer directory of `pack`, the one that wins first: active mods from last to first, the legacy `mod/`, then `pack`.
std::vector<std::filesystem::path> Layers(const std::filesystem::path& pack);

// A content hash of the directory `modDirectory`: SHA-256 over the sorted relative file names and the hash of each file.
// Empty when a file cannot be read.
std::string ModContentHash(const std::filesystem::path& modDirectory);

// The file for `logical`: the first layer's regular file, otherwise the pack's (which may not exist;
// the caller reports it). `logical` is relative and uses `/`.
std::filesystem::path Resolve(const std::filesystem::path& pack, const std::filesystem::path& logical);
// Resolve while checking a proposed mod list, before publishing it.
std::filesystem::path Resolve(const std::filesystem::path& pack, const std::filesystem::path& logical, std::span<const std::string> mods);

// Resolve for a loader that is handed one pack subdirectory (`<pack>/sprites`) instead of the pack: `file` is relative to it.
std::filesystem::path ResolveIn(const std::filesystem::path& directory, const std::filesystem::path& file);

// The stems of the `extension` files (".json") in the logical directory `directory` (`fonts`) of all layers: each name once, sorted.
// A missing directory contributes nothing. Used by drivers that enumerate what the loaders would read.
std::vector<std::string> ListStems(const std::filesystem::path& pack, const std::filesystem::path& directory, std::string_view extension);

} // namespace obl::pack_path
