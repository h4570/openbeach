#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The one resolver for logical pack paths (`maps/1/map.json`, `gui/layouts.json`, ...). Layers, first match wins:
//   1. `<game dir>/mod/<logical>`        user/mod files shadow the pack's,
//   2. `<game dir>/packs/<id>/<logical>` the selected pack (`base`, the converted one, unless OBL_PACK names another).
// `pack` is the pack directory (`<game dir>/packs/<id>`); the mod layer is `<pack>/../../mod`. Every loader of pack data
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

// The mod layer directory belonging to `pack`.
std::filesystem::path ModLayer(const std::filesystem::path& pack);

// The file for `logical`: the mod layer's file when it exists as a regular file, otherwise the pack's (which may not exist;
// the caller reports it). `logical` is relative and uses `/`.
std::filesystem::path Resolve(const std::filesystem::path& pack, const std::filesystem::path& logical);

// Resolve for a loader that is handed one pack subdirectory (`<pack>/sprites`) instead of the pack: `file` is relative to it.
std::filesystem::path ResolveIn(const std::filesystem::path& directory, const std::filesystem::path& file);

// The stems of the `extension` files (".json") in the logical directory `directory` (`fonts`) of both layers: each name once, sorted.
// A missing directory contributes nothing. Used by drivers that enumerate what the loaders would read.
std::vector<std::string> ListStems(const std::filesystem::path& pack, const std::filesystem::path& directory, std::string_view extension);

} // namespace obl::pack_path
