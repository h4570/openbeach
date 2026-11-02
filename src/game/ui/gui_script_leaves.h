#pragma once
#include "game/ui/gui_script_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"

#include <cstdint>

namespace obl::gui {

// The leaves GuiScriptParse calls (gui_script_parse.cpp); each counts as ui.gui-script-leaves.
namespace gui_script_leaves {
void ParseTab(ui::GuiScript* s);
void ParseColumn(ui::GuiScript* s);
bool ParseCommand(ui::GuiScript* s, int32_t command); // true: StartDebug
} // namespace gui_script_leaves
} // namespace obl::gui
