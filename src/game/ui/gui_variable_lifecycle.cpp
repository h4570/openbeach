#include "gui_widgets.h"
#include "game/ui/gui_widget_virtual.h"
#include "gui_variable_lifecycle.h"

#include "game/game/game_accessors.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"

#include <cstring>

namespace obl::gui {
namespace {
using Variable = ui::GuiVariable;
using Image = ui::GuiVariableImage;
using Base = ui::GuiWidget;

// Call counters (`call-report <name>`).
test::CallCounter g_variableCounter("ui.gui-variable-lifecycle");
test::CallCounter g_imageCounter("ui.gui-variable-image-lifecycle");

void SetVisible(Image* image, bool visible)
{
    if (visible) {
        image->Invalidate();
    }
    image->SetVisibleBase(visible);
}

} // namespace

Variable* GuiVariableConstruct(
        Variable* variable, int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t align)
{
    g_variableCounter.Count();
    return new (static_cast<void*>(variable)) Variable(x0, y0, x1, y1, key, font, color, align);
}
Image* GuiVariableImageConstruct(Image* image, int x0, int y0, int x1, int y1, const char* key)
{
    g_imageCounter.Count();
    return new (static_cast<void*>(image)) Image(x0, y0, x1, y1, key);
}

} // namespace obl::gui

namespace obl::ui {

GuiVariable::GuiVariable(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t align)
    : GuiLeaf(x0, y0, x1, y1)
{
    m_font = font;
    m_color = color;
    m_alignFlags = align;
    std::memcpy(m_key, key, std::strlen(key) + 1);
    m_type = 0xd;
}

GuiVariable::~GuiVariable()
{
    gui::g_variableCounter.Count();
}

GuiVariableImage::GuiVariableImage(int x0, int y0, int x1, int y1, const char* key) : GuiLeaf(x0, y0, x1, y1)
{
    std::memcpy(m_key, key, std::strlen(key) + 1);
    m_type = 0xe;
}

GuiVariableImage::~GuiVariableImage()
{
    gui::g_imageCounter.Count();
}

void GuiVariableImage::SetVisible(uint32_t visible)
{
    gui::g_imageCounter.Count();
    gui::SetVisible(this, visible != 0);
}

} // namespace obl::ui
