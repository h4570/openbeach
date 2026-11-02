#pragma once

#include "game/ui/gui_manager_types.h"

#include <cstdint>

namespace obl::gui_widgets {

struct Rect {
    int32_t x0, y0, x1, y1;
};

inline int32_t Add(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
inline int32_t Sub(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

inline void Construct(ui::GuiWidget& w, int x0, int y0, int x1, int y1)
{
    w.m_x0 = w.m_baseX0 = x0;
    w.m_y0 = w.m_baseY0 = y0;
    w.m_x1 = w.m_baseX1 = x1;
    w.m_y1 = w.m_baseY1 = y1;
    w.m_hoverTime = 0.0f;
    w.m_visible = 0;
    w.m_tooltipX = 0;
    w.m_tooltipY = 0;
    w.m_tooltip = nullptr;
    w.m_hovered = 0;
    w.m_held = 0;
    w.m_clicked = 0;
    w.m_dirty = 1;
}

// The base widget's SetOrigin: current rectangle = base rectangle moved by (dx, dy). The base rectangle stays.
inline void SetOrigin(ui::GuiWidget& w, int dx, int dy)
{
    w.m_x0 = Add(w.m_baseX0, dx);
    w.m_y0 = Add(w.m_baseY0, dy);
    w.m_x1 = Add(w.m_baseX1, dx);
    w.m_y1 = Add(w.m_baseY1, dy);
}

// The base widget's Translate: moves the current rectangle only (window dragging); the base rectangle stays.
inline void Translate(ui::GuiWidget& w, int dx, int dy)
{
    w.m_x0 = Add(w.m_x0, dx);
    w.m_x1 = Add(w.m_x1, dx);
    w.m_y0 = Add(w.m_y0, dy);
    w.m_y1 = Add(w.m_y1, dy);
}

// SetPosition (not a virtual): puts the top-left corner at (x, y) and keeps the size (current rectangle only).
inline void SetPosition(ui::GuiWidget& w, int x, int y)
{
    const int32_t width = Sub(w.m_x1, w.m_x0);
    const int32_t height = Sub(w.m_y1, w.m_y0);
    w.m_x0 = x;
    w.m_y0 = y;
    w.m_x1 = Add(x, width);
    w.m_y1 = Add(y, height);
}

inline Rect GetBounds(const ui::GuiWidget& w)
{
    return {w.m_x0, w.m_y0, w.m_x1, w.m_y1};
}

// The base widget's HitTest: inclusive on all four edges. Only a hit writes the hover flag; a miss leaves the previous value
// (nothing in the base clears it, derived classes and `GuiManager` do).
inline bool HitTest(ui::GuiWidget& w, int x, int y)
{
    if (x > w.m_x1 || x < w.m_x0 || y > w.m_y1 || y < w.m_y0) return false;
    w.m_hovered = 1;
    return true;
}

inline void Invalidate(ui::GuiWidget& w)
{
    w.m_dirty = 1;
}

inline bool SetVisibleFlag(ui::GuiWidget& w, uint8_t visible)
{
    w.m_visible = visible;
    return visible == 0;
}

template<class Container, class Env> bool ContainerOnMouseLeft(Container& container, int x, int y, int state, Env& env)
{
    auto* node = container.m_children->m_next;
    bool handled = false;
    while (node != container.m_children) {
        if (handled) break;
        handled = env.OnMouseLeft(*node->m_widget, x, y, state);

        node = node->m_next;
    }
    return handled;
}

template<class Container, class Env> void ContainerDrawOverlay(Container& container, Env& env)
{
    auto* node = container.m_children->m_next;
    while (node != container.m_children) {
        env.DrawOverlay(*node->m_widget);
        node = node->m_next;
    }
}

template<class Env> void DestroyWidget(ui::GuiWidgetListNode& node, Env& env)
{
    if (node.m_widget && static_cast<uint32_t>(node.m_widget->m_type) <= 0x16) env.DeletingDestruct(*node.m_widget, 1);
}

template<class Env> void SetVisibleInvalidate(ui::GuiWidget& w, uint32_t visible, Env& env)
{
    env.SetVisible(w, static_cast<uint8_t>(visible));
    if (static_cast<uint8_t>(visible)) env.Invalidate(w);
}

inline float TooltipDelay(const ui::GuiManager& gui)
{
    return gui.m_tooltipCooldown > 0.0f ? 0.05f : 1.5f;
}

template<class Env> bool TrackTooltip(ui::GuiWidget& w, int x, int y, int state, Env& env)
{
    if (w.m_tooltip) {
        const float dt = env.DeltaTime();
        if (!env.HitTest(w, x, y)) {
            env.Hide(w);
            w.m_hoverTime = 0.0f;
        } else {
            const float delay = env.Delay();
            if (!(w.m_hoverTime >= delay)) {
                if (x == w.m_tooltipX && y == w.m_tooltipY)
                    w.m_hoverTime += dt;
                else
                    w.m_hoverTime = 0.0f;
            }
            if ((state == 1 || state == 2 || state == 3) && w.m_hoverTime >= delay) {
                env.Hide(w);
                w.m_hoverTime = 7.0f;
            }
            if (w.m_hoverTime >= 7.0f)
                env.Hide(w);
            else if (w.m_hoverTime >= delay) {
                env.Show(w, x, y, w.m_tooltip);
                w.m_hoverTime += dt;
            }
        }
    }
    w.m_tooltipX = x;
    w.m_tooltipY = y;
    return false;
}

template<class Env> bool HideTooltipOnClick(ui::GuiWidget& w, int state, Env& env)
{
    if (w.m_tooltip && env.IsHovered(w)) {
        const float delay = env.Delay(); // even when state is not a press
        if ((state == 1 || state == 2) && !(delay > w.m_hoverTime)) {
            env.Hide(w);
            w.m_hoverTime = 7.0f;
        }
    }
    return false;
}

} // namespace obl::gui_widgets

namespace obl::gui {
// A GUI record for an in-place constructor (the Envs' `NewRecord<T>`): `new T()` value-initializes it, every field zero, so no
// field keeps old heap bytes. The constructor runs over it, so the record's own destructor never runs and frees nothing.
template<class T> T* NewGuiRecord()
{
    return new T();
}
} // namespace obl::gui
