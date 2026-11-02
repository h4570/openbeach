#pragma once
#include "core/char16.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/gui_script_types.h"

#include <cstdint>

namespace obl::ui {
struct GuiWidget;
} // namespace obl::ui

namespace obl::gui {
// The game frame draws the credits roll over the GUI (the tooltip is ui::GuiTooltip::Draw).
void CreditsDraw(ui::GuiCredits* credits);
// A list node holding `widget`.
ui::GuiScriptNode* GuiListNodeConstruct(ui::GuiScriptNode* node, ui::GuiWidget* widget);
// The image list frees its entries through this (flags bit 0 frees the entry).
ui::GuiImageListEntry* GuiImageListEntryDelete(ui::GuiImageListEntry* entry, uint8_t flags);
// The literal-text constructor, the dialog node allocation and the ticker list constructor of the manager, dialog and
// list files.
ui::GuiText* GuiTextConstructLiteral(
        ui::GuiText* label, int x0, int y0, int x1, int y1, const Char16* text, int font, uint32_t color, uint32_t flags);
ui::GuiManagerDialogNode* GuiAllocDialogNode(ui::GuiManagerDialogEntry** entry);
ui::TickerList* TickerListConstruct(ui::TickerList* ticker);
void SetProgressVisibleUnlessSandbox(void* gui);
void UpdateGuiBarHeight(void* gui);

void LoadingBarSetProgress(float progress);
// The OnMouseWheel of every GUI mode and widget class without a wheel handler of its own: it never takes the event.
bool GuiWidgetOnMouseWheel(ui::GuiWidget* widget, int x, int y, int delta);
} // namespace obl::gui
