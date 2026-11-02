#pragma once

#include <cstdint>

namespace obl::ui {
struct GuiBar;
struct GuiImage;
} // namespace obl::ui

namespace obl::gui {
// The image and bar constructors and the image's SetFrame and SetSprite: the GUI files call them directly (the constructors write
// into the memory the caller allocated and return it).
ui::GuiImage* ImageConstruct(ui::GuiImage* w, int x0, int y0, int x1, int y1, const char* name);
void GuiImageSetFrame(ui::GuiImage* w, int frame);
// The image list sets its entries' sprites directly.
void GuiImageSetSprite(ui::GuiImage* w, const char* name);
ui::GuiBar* GuiBarConstruct(ui::GuiBar* w, int x0, int y0, int x1, int y1, float min, float max, const char* key, int mode);
} // namespace obl::gui
