#pragma once
#include <cstdint>

namespace obl::ui {
struct GuiVariable;
struct GuiVariableImage;
} // namespace obl::ui

namespace obl::gui {
// The variable text and variable image constructors: the script parser and the table builder call them directly.
ui::GuiVariable* GuiVariableConstruct(
        ui::GuiVariable* variable, int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t align);
ui::GuiVariableImage* GuiVariableImageConstruct(ui::GuiVariableImage* variable, int x0, int y0, int x1, int y1, const char* key);
} // namespace obl::gui
