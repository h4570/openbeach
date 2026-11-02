#include "core/char16.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/renderer_api.h"
#include "game/ui/gui_widgets_image_bar.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_trivial_slots.h"
#include "game/game/game_accessors.h"
#include "game/game/host_text.h"
#include "game/ui/gui_canvas_live.h"
#include "game/ui/gui_widgets_leaves.h"
#include "core/log.h"
#include "core/call_counter.h"

#include <cstring>
#include <cwchar>

namespace obl::gui {
namespace {
using Image = ui::GuiImage;
using Bar = ui::GuiBar;
using Base = ui::GuiWidget;
using Instance = ui::SpriteInstance;
using Gui = ui::GuiManager;

// Call counters (`call-report <name>`); the frame driver is testing/gui_image_bar_frame.cpp.
test::CallCounter g_imageCalls("ui.gui-image");
test::CallCounter g_barCalls("ui.gui-bar");

ui::SpriteBank* Bank()
{
    return ui::GetSpriteBank();
}
Instance* Create(const char* name)
{
    return (static_cast<ui::SpriteBank*>(Bank()))->CreateObservedInstance(name);
}
void Release(ui::SpriteBank* bank, void*, Instance* instance)
{
    (static_cast<ui::SpriteBank*>(bank))->ReleaseInstance(instance);
}

void ResizeImage(Image* w, bool base)
{
    const auto* anim = w->m_instance->m_sprite->m_anims[w->m_instance->m_animIndex];
    w->m_x1 = gui_widgets::Add(w->m_x0, anim->m_width);
    w->m_y1 = gui_widgets::Add(w->m_y0, anim->m_height);
    if (base) {
        w->m_baseX1 = w->m_x1;
        w->m_baseY1 = w->m_y1;
    }
}
void ImageFrame(Image* w, int frame)
{
    w->m_instance->m_animType = 3;
    w->m_instance->m_frameIndex = frame;
    w->Invalidate();
}
void ImageSprite(Image* w, const char* name)
{
    if (std::strcmp(w->m_name, name) == 0) return;
    std::memcpy(w->m_name, name, std::strlen(name) + 1);
    Release(Bank(), nullptr, w->m_instance);
    w->m_instance = Create(w->m_name);
    ResizeImage(w, false);
    w->Invalidate();
}
struct MouseEnv {
    void TrackTooltip(ui::GuiImageData& w, int x, int y, int state) { w.TrackTooltip(x, y, state); }
    void HitTest(ui::GuiImageData& w, int x, int y) { w.HitTest(x, y); }
    bool Hovered(ui::GuiImageData& w) { return w.IsHovered(); }
};
bool ImageMouse(Image* w, int x, int y, int state)
{
    MouseEnv env;
    return gui_widgets::ImageMouseLeft(*w, x, y, state, env);
}
void BarDraw(Bar* w, gui_canvas::Canvas& canvas)
{
    void* provider = w->m_provider;
    if (!provider) provider = static_cast<ui::GuiManager*>(game::GetGuiManager())->VariableProvider();
    if (provider) {
        Char16 text[512];
        provider_virtual::GetVariableText(provider, w->m_variable, text);
        Char16* end;
        const double value = ui::host_text::ParseNumber(text, &end);
        auto pack = [](int r, int g, int b) { return obl::ui::renderer::PackColor(game::GetRenderer(), r, g, b); };
        gui_widgets::DrawBar(*w, value, pack, canvas);
    }
    w->m_dirty = 0;
}

} // namespace

Image* ImageConstruct(Image* w, int x0, int y0, int x1, int y1, const char* name)
{
    g_imageCalls.Count();
    return new (static_cast<void*>(w)) Image(x0, y0, x1, y1, name);
}

void GuiImageSetSprite(Image* w, const char* name)
{
    g_imageCalls.Count();
    ImageSprite(w, name);
}
void GuiImageSetFrame(Image* w, int frame)
{
    g_imageCalls.Count();
    ImageFrame(w, frame);
}
Bar* GuiBarConstruct(Bar* w, int x0, int y0, int x1, int y1, float min, float max, const char* key, int mode)
{
    g_barCalls.Count();
    return new (static_cast<void*>(w)) Bar(x0, y0, x1, y1, min, max, key, mode);
}

} // namespace obl::gui

namespace obl::ui {
using gui::g_barCalls;
using gui::g_imageCalls;

GuiImage::GuiImage(int x0, int y0, int x1, int y1, const char* name) : GuiLeaf(x0, y0, x1, y1)
{
    std::memcpy(m_name, name, std::strlen(name) + 1);
    m_instance = gui::Create(m_name);
    m_type = 10;
    gui::ResizeImage(this, true);
}
GuiImage::~GuiImage()
{
    g_imageCalls.Count();
    if (!m_instance) return; // a default-constructed record owns nothing
    GuiWidget::SetVisibleBase(0);
    gui::Release(gui::Bank(), nullptr, m_instance);
}
void GuiImage::Draw()
{
    g_imageCalls.Count();
    gui::LiveCanvas canvas;
    gui_widgets::DrawImage(*this, canvas.BorrowSprite(m_instance), canvas);
}
bool GuiImage::HitTest(int x, int y)
{
    g_imageCalls.Count();
    return gui_widgets::ImageHitTest(*this, x, y);
}
bool GuiImage::MouseLeft(int x, int y, int state)
{
    g_imageCalls.Count();
    return gui::ImageMouse(this, x, y, state);
}
GuiBar::GuiBar(int x0, int y0, int x1, int y1, float min, float max, const char* key, int mode) : GuiLeaf(x0, y0, x1, y1)
{
    m_min = min;
    m_max = max;
    m_colorMode = mode;
    m_lastValue = -1;
    std::memcpy(m_variable, key, std::strlen(key) + 1);
    m_width = gui_widgets::Sub(x1, x0);
    m_height = gui_widgets::Sub(y1, y0);
    m_type = 18;
}
GuiBar::~GuiBar()
{
    g_barCalls.Count();
}
void GuiBar::Draw()
{
    g_barCalls.Count();
    gui::LiveCanvas canvas;
    gui::BarDraw(this, canvas);
}
} // namespace obl::ui
