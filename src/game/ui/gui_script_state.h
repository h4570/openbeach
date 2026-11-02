#pragma once

#include "game/ui/gui_script_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"

namespace obl::gui {
// The script's constructor and its dialog and widget lists: the GUI manager calls them directly.
ui::GuiScript* GuiScriptConstruct(ui::GuiScript* script, ui::GuiManager* manager);
ui::GuiScriptNode* GuiScriptDialogs(ui::GuiScript& script);
ui::GuiScriptNode* GuiScriptWidgets(ui::GuiScript& script);
// The parent stack and the widget registration: the parser files call them directly.
void GuiScriptPushParent(ui::GuiScript& script, ui::GuiWidget* widget);
void GuiScriptPopParent(ui::GuiScript& script);
void GuiScriptAddWidget(ui::GuiScript& script, ui::GuiWidget* widget, bool dialog);
// Frees a parser list whose widgets are borrowed or owned elsewhere: payloads are cleared before each node is destroyed.
void ReleaseBorrowedList(ui::GuiScriptNode*& head);
} // namespace obl::gui
