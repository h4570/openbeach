#include "core/char16.h"
#include "game/ui/renderer_api.h"
#include "gui_widgets_leaves.h"
#include "game/ui/gui_data.h"
#include "gui_widgets.h"

#include "game/game/game_accessors.h"
#include "game/ui/image.h"
#include "gui_canvas_live.h"
#include "game/ui/gui_widgets_leaves.h"
#include "core/call_counter.h"

#include <cstring>
#include <string>
#include "game/ui/text_table.h"

namespace obl::gui {
namespace {
using Text = ui::GuiText;
using Tga = ui::GuiTga;
using Base = ui::GuiWidget;

// Call counters (`call-report <name>`).
test::CallCounter g_leafCalls("ui.gui-leaves");
test::CallCounter g_textCalls("ui.gui-text");
test::CallCounter g_tgaCalls("ui.gui-tga");

const Char16* Lookup(const char* key)
{
    return ui::text_table::Lookup(key);
}

struct TgaEnv {
    void SetVisible(ui::GuiTgaData& w, uint32_t visible) { w.SetVisibleBase(visible); }
    void* Load(const char* name)
    {
        const std::string path = std::string(gui_data::g_ScreenImagePath) + name;
        return ui::images::LoadTga(path.c_str());
    }
    void BuildPalette(void* image) { ui::images::BuildPalette16(image, game::GetRenderer()->m_rgb555 == 1); }
    void DestroyImage(void* image) { ui::images::Free(image); }
    void DrawImage(void* image, int x, int y, int left, int top, int right, int bottom, int mode, uint32_t flags, uint32_t mask)
    {
        const int pos[] = {x, y}, rect[] = {left, top, right, bottom};
        obl::ui::renderer::DrawImage(game::GetRenderer(), pos, rect, image, mode, flags, mask);
    }
};
void TgaVisible(Tga* w, uint32_t visible)
{
    TgaEnv env;
    gui_widgets::SetTgaVisible(*w, visible, env);
}
void TgaDraw(Tga* w)
{
    TgaEnv env;
    gui_widgets::DrawTga(*w, env);
}
} // namespace

Text* TextConstruct(Text* w, int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t flags)
{
    g_leafCalls.Count();
    g_textCalls.Count();
    return new (static_cast<void*>(w)) Text(x0, y0, x1, y1, key, font, color, flags);
}

Tga* GuiTgaConstruct(Tga* w, int x0, int y0, int x1, int y1, const char* name)
{
    g_leafCalls.Count();
    g_tgaCalls.Count();
    return new (static_cast<void*>(w)) Tga(x0, y0, x1, y1, name);
}

} // namespace obl::gui

namespace obl::ui {
using gui::g_leafCalls;
using gui::g_textCalls;
using gui::g_tgaCalls;

GuiText::GuiText(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t flags) : GuiLeaf(x0, y0, x1, y1)
{
    m_drawOnSurface = 1;
    if (key) m_text = gui::Lookup(key);
    m_color = color;
    m_font = font;
    m_alignFlags = flags;
    m_type = 12;
}
GuiText::~GuiText()
{
    g_leafCalls.Count();
    g_textCalls.Count();
    if (m_ownedText) ::operator delete(m_ownedText);
}
void GuiText::Draw()
{
    g_leafCalls.Count();
    g_textCalls.Count();
    gui::LiveCanvas canvas;
    gui_widgets::DrawText(*this, false, canvas);
}
void GuiText::DrawOverlay()
{
    g_leafCalls.Count();
    g_textCalls.Count();
    gui::LiveCanvas canvas;
    gui_widgets::DrawText(*this, true, canvas);
}
GuiTga::GuiTga(int x0, int y0, int x1, int y1, const char* name) : GuiLeaf(x0, y0, x1, y1)
{
    std::memcpy(m_name, name, std::strlen(name) + 1);
    m_type = 16;
}
GuiTga::~GuiTga()
{
    g_leafCalls.Count();
    g_tgaCalls.Count();
    if (m_visible || m_image) gui::TgaVisible(this, 0); // a hidden widget without an image holds nothing
}
void GuiTga::SetVisible(uint32_t visible)
{
    g_leafCalls.Count();
    g_tgaCalls.Count();
    gui::TgaVisible(this, visible);
}
void GuiTga::Draw()
{
    g_leafCalls.Count();
    g_tgaCalls.Count();
    gui::TgaDraw(this);
}
} // namespace obl::ui
