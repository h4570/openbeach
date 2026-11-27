#pragma once

#include "game/ui/gui_patch.h"

#include <string_view>
#include <vector>

// OpenBeach's own layer of the GUI: under the mods and above the pack. It adds the "Mods" button to the main menu and the mods
// screen (docs/modding/players/mods-screen.md) through the same GUI patches mods use (gui_patch.h), so it needs no change to the
// pack's files and works on the converted pack and on L0 alike. Its texts are own texts (game/own_texts.h). It ships no graphics:
// its controls draw themselves (own_widgets.h). Pure logic: no game, no files.
namespace obl::own_layer {

// The patch source's mod id in error messages; never a mod id (mod ids have no capital letters).
inline constexpr const char* kLayerName = "OpenBeach";
// The dialog and controls it adds, which mods may move or hide by id like any other.
inline constexpr const char* kModsButtonId = "mods_button";
inline constexpr const char* kModsDialog = "ModsScreen";
inline constexpr const char* kModsPanelId = "mods_panel";
inline constexpr const char* kMoreScenariosButtonId = "more_scenarios_button";
inline constexpr const char* kExtraScenariosDialog = "ExtraScenarios";
inline constexpr const char* kExtraScenariosPanelId = "extra_scenarios_panel";

// The layer's patches of `layout` (a key of gui/layouts.json), applied before the mods' patches; empty for a layout it leaves alone.
// `hasExtraScenarios` enables the ScenarioScreen entry point and ExtraScenarios dialog when mod scenario sets are loaded.
std::vector<gui_patch::PatchSource> GuiPatches(std::string_view layout, bool hasExtraScenarios = false);

} // namespace obl::own_layer
