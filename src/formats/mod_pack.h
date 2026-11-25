#pragma once

#include "core/settings_schema.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Mods as packages (docs/modding/reference/mods.md): a directory `<game dir>/mods/<id>/` with a `mod.json` manifest next to the files
// it adds or replaces under their logical paths. Pure logic: no Windows APIs.
namespace obl::mod_pack {

inline constexpr const char* kFormat = "obl.mod";
inline constexpr int kVersion = 1;
// The newest presentation script API the game offers (docs/modding/reference/scripting.md); a mod naming a newer one is refused.
inline constexpr int kPresentationApi = 1;
// Where a mod's presentation scripts live, relative to its directory.
inline constexpr const char* kPresentationScripts = "scripts/presentation";

// Settings a mod may declare in mod.json (docs/modding/reference/mods.md#settings).
inline constexpr size_t kMaxSettings = 32;

// What a mod may change. A world mod is recorded in saves and replays; a presentation mod reaches the simulation only through
// game commands, so it ships no data the simulation reads and saves and replays leave it out (docs/architecture/mods/index.md).
enum class Scope { World, Presentation };

struct Manifest {
    std::string id;
    std::string title;
    std::string modVersion;
    std::vector<std::string> packs; // the pack ids the mod starts on (`pack` or `packs`)
    std::vector<std::string> requiredMods; // mod ids that must be active and earlier in the load order
    Scope scope = Scope::World;
    std::vector<settings_schema::Field> settings; // `settings`: kept in openbeach.ini under [Mod.<id>], never read by the simulation
    int api = 0; // the presentation API version the scripts were written for; 0 when the manifest names none
};

// What the start-up check learned about an active mod.
struct ActiveMod {
    Manifest manifest;
    std::string hash; // pack_path::ModContentHash, computed once
    std::filesystem::path directory; // `<game dir>/mods/<id>`
};

// Whether saves and replays record the mod: only world mods change what the simulation computes.
inline bool Recorded(const ActiveMod& mod)
{
    return mod.manifest.scope == Scope::World;
}

// False with `error` when `text` is not a valid mod.json.
bool ParseManifest(std::string_view text, Manifest& manifest, std::string& error);

// Checks that every id in `active` names an existing mod directory with a valid manifest whose id is the directory name,
// lists `packId`, and whose `requires` are active and earlier; a presentation mod ships only what that scope allows, and a mod
// with presentation scripts names an API version the game has. On success `mods` holds them in load order; on failure
// `error` names the mod and the reason.
bool CheckActive(const std::filesystem::path& gameDirectory, std::string_view packId, std::span<const std::string> active,
        std::vector<ActiveMod>& mods, std::string& error);

// The result of the last successful start-up check (SetActive), in load order.
const std::vector<ActiveMod>& Active();
// Test support (`claim-mods`): replaces what saves and replays record and compare as the active mods, without touching the
// layers. RestoreActive brings back the start-up check's own result.
void OverrideActive(std::vector<ActiveMod> mods);
void RestoreActive();

// The start-up step: reads the mod list (OBL_MODS, else `configured`), checks it, and makes the mods layers of pack_path.
// False with `error` when the check fails; the game stops then.
bool Activate(const std::filesystem::path& gameDirectory, std::string_view packId, std::string_view configured, std::string& error);

} // namespace obl::mod_pack
