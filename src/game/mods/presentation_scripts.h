#pragma once

#include <string>
#include <vector>

namespace obl::ui {
struct Input;
}

// The game's side of the presentation scripts (script_runtime.h): the running mods' scripts get every frame and the player's input,
// and their commands go into the game loop's queue like the GUI's. Nothing here runs inside the simulation's tick.
namespace obl::mods {

// Called at the end of every game update, after the state's update. The first call starts the scripts of the active mods
// (mod_pack::Active(), so after the start-up check); then each call hands on the input that changed since the last one, then the
// frame. `input` may be null.
void PresentationFrame(float dt, const ui::Input* input);

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
