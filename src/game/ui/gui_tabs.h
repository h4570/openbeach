#pragma once

#include "core/pointer_width.h"
#include "game/ui/gui_canvas.h"
#include "game/ui/gui_containers.h"
#include "game/ui/gui_containers_types.h"

#include <cstdint>

// `GuiTab` and `GuiTabWindow`: the pages of the staff and building dialogs. A tab is a container of a `std::list` of
// widgets with a two-direction button sprite; a tab window keeps a singly linked list of tabs and shows one of them. All game-heap and
// virtual-call work goes through an `Env`, so the unit tests run the same code on plain memory.
namespace obl::gui_tabs {

using gui_containers::FirstChildAnswer;
using gui_containers::ForEachChild;
using Node = ui::GuiContainerChildNode;
using Widget = ui::GuiWidget;
using Tab = ui::GuiTabData;
using TabWindow = ui::GuiTabWindowData;
using TabNode = ui::GuiTabNode;

inline constexpr int32_t kTabType = 9;
inline constexpr int32_t kTabWindowType = 8;
inline constexpr int32_t kListType = 2; // GuiList (ComboBox): drawn after its siblings so an open drop-down covers them
inline constexpr int32_t kPageShown = 1; // GuiTab::m_state of the current page
inline constexpr int32_t kNeverDrawn = 3; // what Invalidate writes to m_drawnState: no tab state, so the sprite is redrawn
inline constexpr size_t kTabNodeBytes = sizeof(TabNode); // allocation size of a tab window's list node (next, widget)

// A page is live only while it is shown and visible; hidden pages do not tick or refresh.
inline bool PageActive(const Tab& tab)
{
    return tab.m_visible != 0 && tab.m_state == kPageShown;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// GuiTab (type 9)

// The rectangle comes from GuiWidget's constructor, the tooltip goes with GuiLeaf's destructor.
template<class Env> void TabConstruct(Tab& tab, const char* sprite, Env& env)
{
    gui_containers::InitList(tab.m_children, env);
    tab.m_state = 0;
    tab.m_sprite = env.CreateSprite(sprite);
    tab.m_type = kTabType;
}

// The button sprite goes back first, then the list nodes (children belong to the parser and the dialog).
template<class Env> void TabDestruct(Tab& tab, Env& env)
{
    if (tab.m_sprite) env.ReleaseSprite(tab.m_sprite);
    gui_containers::DestroyList(tab.m_children, env);
}

template<class Env> void TabSetOrigin(Tab& tab, int dx, int dy, Env& env)
{
    env.BaseSetOrigin(tab, dx, dy);
    ForEachChild(tab.m_children, [&](Widget& w) { env.SetOrigin(w, dx, dy); });
}

template<class Env> void TabTranslate(Tab& tab, int dx, int dy, Env& env)
{
    env.BaseTranslate(tab, dx, dy);
    ForEachChild(tab.m_children, [&](Widget& w) { env.Translate(w, dx, dy); });
}

// The wheel goes to the first child that takes it, whether or not the page is shown.
template<class Env> uint8_t TabOnMouseWheel(Tab& tab, int x, int y, int delta, Env& env)
{
    return FirstChildAnswer(tab.m_children, [&](Widget& w) { return env.OnMouseWheel(w, x, y, delta); });
}

// The argument is passed to the base and the children as the whole 32-bit value; only its low byte decides the state.
template<class Env> void TabSetVisible(Tab& tab, uint32_t visible, Env& env)
{
    env.BaseSetVisible(tab, visible);
    tab.m_state = static_cast<uint8_t>(visible) ? kPageShown : 0;
    ForEachChild(tab.m_children, [&](Widget& w) { env.SetVisible(w, visible); });
}

template<class Env> void TabUpdate(Tab& tab, uint32_t dt, Env& env)
{
    if (PageActive(tab)) ForEachChild(tab.m_children, [&](Widget& w) { env.Update(w, dt); });
}

template<class Env> void TabRefresh(Tab& tab, Env& env)
{
    if (PageActive(tab)) ForEachChild(tab.m_children, [&](Widget& w) { env.Refresh(w); });
}

// Forces the next Draw to repaint the sprite; it does not touch the widget's own dirty flag.
template<class Env> void TabInvalidate(Tab& tab, Env& env)
{
    const bool shown = tab.m_state == kPageShown;
    tab.m_drawnState = kNeverDrawn;
    if (shown) ForEachChild(tab.m_children, [&](Widget& w) { env.Invalidate(w); });
}

// Draws the children that are not lists, then the lists (an open drop-down must cover the rest). Each pass walks the list afresh.
template<class Callback> void ForEachChildListsLast(Node* const& children, Callback&& callback)
{
    ForEachChild(children, [&](Widget& w) {
        if (w.m_type != kListType) callback(w);
    });
    ForEachChild(children, [&](Widget& w) {
        if (w.m_type == kListType) callback(w);
    });
}

template<class Env> void TabDraw(Tab& tab, gui_canvas::Canvas& canvas, gui_canvas::SpriteId sprite, Env& env)
{
    if (tab.m_state != tab.m_drawnState) {
        int direction = tab.m_state;
        const int directions = canvas.Metrics(sprite).directions;
        if (direction >= directions) direction = directions - 1;
        canvas.SetDirection(sprite, direction);
        canvas.DrawClipped(sprite, tab.m_x0, tab.m_y0, 0);
        tab.m_drawnState = tab.m_state;
    }

    if (tab.m_state == kPageShown) ForEachChildListsLast(tab.m_children, [&](Widget& w) { env.Draw(w); });
}

template<class Env> void TabDrawOverlay(Tab& tab, Env& env)
{
    if (tab.m_state == kPageShown) ForEachChildListsLast(tab.m_children, [&](Widget& w) { env.DrawOverlay(w); });
}

// `TabMouse` (non-virtual, called by the tab window): `active` is the current tab, which also hands the mouse to its page.
// The base tooltip tracker runs first and its answer is ignored; state 3 (blocked by a modal dialog) clears the hover flag without a test.
// An inactive tab answers true only for a click on itself. An active one offers the event to its children first; when none takes it and
// the cursor is over the tab, a click calls every child's RefreshSelection and the answer stays false.
template<class Env> uint8_t TabMouse(Tab& tab, int x, int y, int state, bool active, Env& env)
{
    env.TrackTooltip(tab, x, y, state);
    tab.m_hovered = state == 3 ? 0 : env.HitTest(tab, x, y);
    if (!active) return env.IsHovered(tab) && state == 1;
    const uint8_t taken = FirstChildAnswer(tab.m_children, [&](Widget& w) { return env.OnMouseLeft(w, x, y, state); });
    if (taken) return taken;
    if (env.IsHovered(tab) && state == 1) ForEachChild(tab.m_children, [&](Widget& w) { env.RefreshSelection(w); });
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// GuiTabWindow (type 8)

inline void TabWindowConstruct(TabWindow& tabWindow)
{
    tabWindow.m_type = kTabWindowType;
}

// Every node is unlinked, its widget pointer cleared (so the wrapper's DestroyWidget finds nothing to delete: the tabs belong to the parser
// and the dialog, as do the pages) and the 8-byte block freed.
template<class Env> void TabWindowDestruct(TabWindow& tabWindow, Env& env)
{
    while (TabNode* node = tabWindow.m_tabs) {
        TabNode* next = node->m_next;
        node->m_widget = nullptr;
        tabWindow.m_tabs = next;
        env.Free(node);
    }
}

// The list head is read before the base call in Translate and after it in SetOrigin; the base never touches the list.
template<class Env> void TabWindowTranslate(TabWindow& tabWindow, int dx, int dy, Env& env)
{
    TabNode* head = tabWindow.m_tabs;
    env.BaseTranslate(tabWindow, dx, dy);
    for (TabNode* node = head; node; node = node->m_next) env.Translate(*node->m_widget, dx, dy);
}

template<class Env> void TabWindowSetOrigin(TabWindow& tabWindow, int dx, int dy, Env& env)
{
    env.BaseSetOrigin(tabWindow, dx, dy);
    for (TabNode* node = tabWindow.m_tabs; node; node = node->m_next) env.SetOrigin(*node->m_widget, dx, dy);
}

// Only the current tab sees the wheel.
template<class Env> uint8_t TabWindowOnMouseWheel(TabWindow& tabWindow, int x, int y, int delta, Env& env)
{
    return tabWindow.m_current ? env.OnMouseWheel(*tabWindow.m_current, x, y, delta) : 0;
}

// The current tab goes first (as the active one, so its page gets the click); if it does not take the event, every other tab is asked
// whether the click hit its button. The first that says yes becomes current: the old one is hidden, the new one shown, every dialog is
// invalidated and the button click sound plays.
template<class Env> uint8_t TabWindowOnMouseLeft(TabWindow& tabWindow, int x, int y, int state, Env& env)
{
    uint8_t taken = 0;
    if (tabWindow.m_current) {
        taken = env.TabMouse(static_cast<Tab&>(*tabWindow.m_current), x, y, state, true);
        if (taken) return taken;
    }
    for (TabNode* node = tabWindow.m_tabs; node && !taken; node = node->m_next) {
        Widget* tab = node->m_widget;
        if (tab == tabWindow.m_current) continue;
        taken = env.TabMouse(static_cast<Tab&>(*tab), x, y, state, false);
        if (!taken) continue;
        env.SetVisible(*tabWindow.m_current, 0);
        tabWindow.m_current = tab;
        env.SetVisible(*tab, 1);
        env.InvalidateAllDialogs();
        env.PlayClick();
    }
    return taken;
}

template<class Env> void TabWindowUpdate(TabWindow& tabWindow, uint32_t dt, Env& env)
{
    for (TabNode* node = tabWindow.m_tabs; node; node = node->m_next) env.Update(*node->m_widget, dt);
}

template<class Env> void TabWindowRefresh(TabWindow& tabWindow, Env& env)
{
    for (TabNode* node = tabWindow.m_tabs; node; node = node->m_next) env.Refresh(*node->m_widget);
}

// Showing hides every tab and then shows the current one; hiding hides every tab.
template<class Env> void TabWindowSetVisible(TabWindow& tabWindow, uint32_t visible, Env& env)
{
    env.BaseSetVisible(tabWindow, visible);
    for (TabNode* node = tabWindow.m_tabs; node; node = node->m_next) env.SetVisible(*node->m_widget, 0);
    if (static_cast<uint8_t>(visible) && tabWindow.m_current) env.SetVisible(*tabWindow.m_current, 1);
}

// Switches to the tab at `index` (an index past the end does nothing). No tab-click sound, unlike a mouse switch.
template<class Env> void TabWindowSelectTab(TabWindow& tabWindow, int index, Env& env)
{
    TabNode* node = tabWindow.m_tabs;
    if (!node) return;
    for (int i = 0; i < index; ++i) {
        node = node->m_next;
        if (!node) return;
    }
    Widget* tab = node->m_widget;
    env.SetVisible(*tabWindow.m_current, 0);
    tabWindow.m_current = tab;
    env.SetVisible(*tab, 1);
    env.InvalidateAllDialogs();
}

template<class Env> void TabWindowDraw(TabWindow& tabWindow, Env& env)
{
    for (TabNode* node = tabWindow.m_tabs; node; node = node->m_next) env.Draw(*node->m_widget);
}

// The head is read before the base Invalidate (which only sets the dirty flag).
template<class Env> void TabWindowInvalidate(TabWindow& tabWindow, Env& env)
{
    TabNode* head = tabWindow.m_tabs;
    env.BaseInvalidate(tabWindow);
    for (TabNode* node = head; node; node = node->m_next) env.Invalidate(*node->m_widget);
}

template<class Env> void TabWindowDrawOverlay(TabWindow& tabWindow, Env& env)
{
    for (TabNode* node = tabWindow.m_tabs; node; node = node->m_next) env.DrawOverlay(*node->m_widget);
}

template<class Env> void TabWindowAddChild(TabWindow& tabWindow, Widget* child, Env& env)
{
    if (child->m_type == gui_containers::kDefineType) child = static_cast<ui::GuiDefineData*>(child)->m_children->m_next->m_widget;
    auto* node = static_cast<TabNode*>(env.NewTabNode());
    if (node) {
        node->m_next = nullptr;
        node->m_widget = child;
    }
    if (TabNode* tail = tabWindow.m_tabs) {
        while (tail->m_next) tail = tail->m_next;
        tail->m_next = node;
        return;
    }
    tabWindow.m_tabs = node;
    if (node) tabWindow.m_current = node->m_widget;
}

} // namespace obl::gui_tabs
