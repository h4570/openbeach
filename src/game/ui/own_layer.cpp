#include "game/ui/own_layer.h"

namespace obl::own_layer {

namespace {

// The engine relies on the dialog name MainMenu on every pack (game_states_logic.h shows it), so the button goes into it whatever
// else the pack's menu holds. (290, 0)..(530, 38) hangs from the top edge of the 800x600 screen, centred in the open space above
// the button stack (clear of L0's 40px frame border and of the converted menu's logo and right-side artwork); a mod may move it.
// The screen covers the whole 800x600 front end and draws itself, so it needs none of the pack's sprites.
constexpr const char* kMainMenuPatch = R"json({
  "format": "obl.gui_patch",
  "version": 1,
  "ops": [
    { "op": "add_control", "dialog": "MainMenu", "id": "mods_button",
      "control": "OwnButton(290, 0, 530, 38, OBL_MENU_MODS, ModsScreen)" },
    { "op": "add_dialog",
      "dialog": "DialogItem ModsScreen(0, 0, 800, 600, None, 0, 0) { ModsPanel(0, 0, 800, 600) id=mods_panel }" }
  ]
})json";

} // namespace

std::vector<gui_patch::PatchSource> GuiPatches(std::string_view layout)
{
    // The reset menu reads the same file as the main menu (gui/layouts.json), so it gets the same controls.
    if (layout != "main_menu" && layout != "reset_menu") return {};
    return {{kLayerName, "OpenBeach main menu layer", kMainMenuPatch}};
}

} // namespace obl::own_layer
