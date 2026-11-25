#pragma once

#include <string>
#include <vector>

namespace obl::ui {
struct Input;
}

// The game's side of the presentation scripts (script_runtime.h): the running mods' scripts get every frame and the player's input,
// their commands go into the game loop's queue like the GUI's, and their overlay is drawn over the finished frame. Nothing here runs
// inside the simulation's tick.
namespace obl::mods {

// Called right after the input update, before the commands and the state's update read the input (docs/architecture/input.md). The
// first call starts the scripts of the active mods (mod_pack::Active(), so after the start-up check); then the input of this update
// goes to on_input as events, a consumed event is taken back out of `input`, and the inputs scripts made up go in. `input` may be
// null.
void PresentationInput(ui::Input* input);

// Called at the end of every game update, after the state's update: the scripts' frame.
void PresentationFrame(float dt);

// Draws the scripts' overlay onto the frame, after the GUI and before the present.
void DrawPresentationOverlay();
// Whether the overlay is drawn. On by default; test runs switch it off (golden images must not depend on mods) unless the test
// asks for it (`mod-overlay on`).
void SetOverlayShown(bool shown);

// on_stop and the stores: at the end of the game.
void StopPresentationScripts();

// Sets a setting a running or active mod declares (docs/modding/reference/mods.md#settings): `value` must be a value of the field.
// It is written to openbeach.ini under [Mod.<mod>] (not in scripted test runs, which leave the player's file alone) and the scripts get
// on_setting. False with `error` for an unknown mod or setting or a value the setting does not take. This is what the mods screen calls.
bool SetModSetting(const std::string& mod, const std::string& id, const std::string& value, std::string& error);

// The ids of the mods whose scripts are running, and why others were switched off (one message each).
std::vector<std::string> RunningPresentationMods();
std::vector<std::string> PresentationFailures();

} // namespace obl::mods
