#pragma once

#include "core/char16.h"
#include "game/ui/gui_canvas.h"
#include "game/ui/gui_widgets.h"
#include "core/float_to_int.h"
#include <cmath>
#include <cstdint>

namespace obl::gui_widgets {

inline void DrawImage(ui::GuiImageData& w, gui_canvas::SpriteId sprite, gui_canvas::Canvas& canvas)
{
    if (w.m_dirty) {
        canvas.DrawClipped(sprite, w.m_x0, w.m_y0, 0);
        w.m_dirty = 0;
    }
}

inline bool ImageHitTest(ui::GuiImageData& w, int x, int y)
{
    w.m_hovered = x >= w.m_x0 && x <= w.m_x1 && y >= w.m_y0 && y <= w.m_y1;
    return w.m_hovered != 0;
}

template<class Env> bool ImageMouseLeft(ui::GuiImageData& w, int x, int y, int state, Env& env)
{
    w.m_clicked = w.m_held = 0;
    env.TrackTooltip(w, x, y, state);
    env.HitTest(w, x, y);
    if (state == 1) {
        if (env.Hovered(w)) w.m_clicked = 1;
    } else if (state == 2) {
        if (env.Hovered(w)) w.m_held = 1;
    } else if (state == 3)
        w.m_hovered = 0;
    return false;
}

template<class Pack> void DrawBar(ui::GuiBarData& w, double value, Pack pack, gui_canvas::Canvas& canvas)
{
    if (!(value >= w.m_min)) value = w.m_min;
    if (value > w.m_max) value = w.m_max;
    if (!w.m_dirty && (value == w.m_lastValue || std::isnan(value) || std::isnan(w.m_lastValue))) return;
    w.m_lastValue = static_cast<float>(value);
    const float fraction = static_cast<float>((value - static_cast<double>(w.m_min)) / (static_cast<double>(w.m_max) - w.m_min));
    uint16_t color = 0;
    if (w.m_colorMode == 0)
        color = pack(26, 219, 123);
    else if (w.m_colorMode == 1) {
        const double r = fraction > 0.5f ? (1.0 - fraction) * 2.0 * 255.0 : 255.0;
        const double g = fraction < 0.5f || std::isnan(fraction) ? fraction * 2.0 * 200.0 : 200.0;
        color = pack(FloatToIntWrap(r), FloatToIntWrap(g), 0);
    } else if (w.m_colorMode == 2) {
        const double r = fraction < 0.5f || std::isnan(fraction) ? fraction * 2.0 * 255.0 : 255.0;
        const double g = fraction > 0.5f ? (1.0 - fraction) * 2.0 * 200.0 : 200.0;
        color = pack(FloatToIntWrap(r), FloatToIntWrap(g), 0);
    }
    // An unsigned 64-bit read with a zero high half treats even a negative cached width as unsigned.
    int width = FloatToIntWrap(static_cast<double>(static_cast<uint32_t>(w.m_width)) * fraction);
    if (!width) width = 2;
    const int split = Add(w.m_x0, width), bottom = Add(w.m_y0, w.m_height);
    canvas.FillRect(w.m_x0, w.m_y0, split, bottom, color);
    canvas.FillRect(split, w.m_y0, Add(split, Sub(w.m_width, width)), bottom, pack(220, 232, 255));
    w.m_dirty = 0;
}

inline void ConfigureText(const ui::GuiTextData& w, gui_canvas::Canvas& canvas)
{
    if (w.m_alignFlags & 1)
        canvas.SetTextAlign(w.m_font, 1);
    else if (w.m_alignFlags & 2)
        canvas.SetTextAlign(w.m_font, 0);
    else if (w.m_alignFlags & 4)
        canvas.SetTextAlign(w.m_font, 2);
    canvas.SetTextOutlined(w.m_font, (w.m_alignFlags & 0x40) != 0);
    canvas.SetTextColor(w.m_font, w.m_color);
    canvas.SetTextRect(w.m_font, w.m_x0, w.m_y0, Sub(w.m_x1, w.m_x0), Sub(w.m_y1, w.m_y0));
}

inline void DrawText(ui::GuiTextData& w, bool overlay, gui_canvas::Canvas& canvas)
{
    if (overlay ? w.m_drawOnSurface != 0 : (!w.m_dirty || !w.m_drawOnSurface)) return;
    ConfigureText(w, canvas);
    const Char16* text = w.m_text ? w.m_text : w.m_ownedText;
    if (text) canvas.DrawWrapped(w.m_font, 0, 0, text);
    if (!overlay) w.m_dirty = 0;
}

// Loading is lazy and independent of dirty/visibility flags at draw time. The environment owns the image heap contract.
template<class Env> void SetTgaVisible(ui::GuiTgaData& w, uint32_t visible, Env& env)
{
    env.SetVisible(w, visible);
    if (static_cast<uint8_t>(visible)) {
        if (!w.m_image) {
            w.m_image = env.Load(w.m_name);
            env.BuildPalette(w.m_image);
        }
    } else if (w.m_image) {
        env.DestroyImage(w.m_image);
        w.m_image = nullptr;
    }
}

template<class Env> void DrawTga(const ui::GuiTgaData& w, Env& env)
{
    env.DrawImage(w.m_image, 0, 0, 0, 0, 800, 600, 0, 0, 0);
}

} // namespace obl::gui_widgets

namespace obl::ui {
struct GuiText;
struct GuiTga;
} // namespace obl::ui

namespace obl::gui {
// The TGA image and text widget constructors: the GUI files call them directly.
ui::GuiTga* GuiTgaConstruct(ui::GuiTga* w, int x0, int y0, int x1, int y1, const char* name);
ui::GuiText* TextConstruct(ui::GuiText* w, int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, uint32_t flags);
} // namespace obl::gui
