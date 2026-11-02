#pragma once

#include <cstdint>

#include "game/ui/gui_script_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"

namespace obl::gui {
// The GUI manager calls the parser directly.
void GuiScriptParse(ui::GuiScript& script, const char* text, uint32_t size);
} // namespace obl::gui
