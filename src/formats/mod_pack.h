#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Mods as packages (docs/modding/mods.md): a directory `<game dir>/mods/<id>/` with a `mod.json` manifest next to the files
// it adds or replaces under their logical paths. Pure logic: no Windows APIs.
namespace obl::mod_pack {

inline constexpr const char* kFormat = "obl.mod";
inline constexpr int kVersion = 1;

struct Manifest {
    std::string id;
    std::string title;
    std::string modVersion;
    std::string pack; // the pack id the mod was made for
    std::vector<std::string> requiredMods; // mod ids that must be active and earlier in the load order
};

// What the start-up check learned about an active mod.
struct ActiveMod {
    Manifest manifest;
    std::string hash; // pack_path::ModContentHash, computed once
};

// False with `error` when `text` is not a valid mod.json.
bool ParseManifest(std::string_view text, Manifest& manifest, std::string& error);

// Checks that every id in `active` names an existing mod directory with a valid manifest whose id is the directory name,
// targets `packId`, and whose `requires` are active and earlier. On success `mods` holds them in load order; on failure
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
