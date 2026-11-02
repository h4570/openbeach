#include "gui_widgets.h"
#include "game/ui/gui_widget_virtual.h"
#include "gui_tooltip.h"

#include "game/game/game_accessors.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace obl::gui {
namespace {
using Base = ui::GuiWidget;
using Rect = gui_widgets::Rect;
using Gui = ui::GuiManager;
using Container = ui::GuiContainer;
using ChildNode = ui::GuiContainerChildNode;

// Plain call counters (`call-report <name>`).
test::CallCounter g_counter("ui.gui-widgets");
test::CallCounter g_tooltipsCounter("ui.gui-tooltips");
test::CallCounter g_treeCounter("ui.gui-widget-tree");

void ManagerShow(Gui* gui, Base* widget, int x, int y, const char* text)
{
    gui->m_tooltipOwner = widget;
    auto* tooltip = static_cast<ui::GuiTooltip*>(gui->m_tooltip);
    tooltip->SetText(text);
    tooltip->Show(x, y);
}
void ManagerHide(Gui* gui, Base* widget)
{
    if (gui->m_tooltipOwner != widget) return;
    static_cast<ui::GuiTooltip*>(gui->m_tooltip)->Hide();
    gui->m_tooltipOwner = nullptr;
    gui->m_tooltipCooldown = 2.5f;
}

struct TooltipEnv {
    float DeltaTime() { return game::GetGameClock()->m_deltaTime; }
    bool HitTest(Base& w, int x, int y) { return w.HitTest(x, y) != 0; }
    bool IsHovered(Base& w) { return w.IsHovered() != 0; }
    float Delay() { return game::GetGuiManager()->GetTooltipDelay(); }
    void Hide(Base& w) { game::GetGuiManager()->HideTooltip(&w); }
    void Show(Base& w, int x, int y, const char* text) { game::GetGuiManager()->ShowTooltip(&w, x, y, text); }
};

void RunSetTooltip(Base* widget, const char* text)
{
    const size_t size = std::strlen(text) + 1;
    widget->m_tooltip = static_cast<char*>(::operator new(size));

    std::memcpy(widget->m_tooltip, text, size);
}

} // namespace

namespace {

void RunSetVisible(Base* widget, uint8_t visible)
{
    if (!gui_widgets::SetVisibleFlag(*widget, visible)) return;
    game::GetGuiManager()->HideTooltip(widget);
    widget->m_hoverTime = 0.0f;
}

struct WidgetTreeEnv {
    bool OnMouseLeft(Base& w, int x, int y, int state) { return w.MouseLeft(x, y, state) != 0; }
    void DrawOverlay(Base& w) { w.DrawOverlay(); }
    void DeletingDestruct(Base& w, uint32_t flags) { gui::DestroyWidget(w, flags); }
    void SetVisible(Base& w, uint8_t visible) { RunSetVisible(&w, visible); }
    void Invalidate(Base& w) { w.Invalidate(); }
};

} // namespace

} // namespace obl::gui

namespace obl::ui {
using gui::g_counter;
using gui::g_tooltipsCounter;

void GuiManager::ShowTooltip(GuiWidget* widget, int x, int y, const char* text)
{
    g_tooltipsCounter.Count();
    g_counter.Count();
    gui::ManagerShow(this, widget, x, y, text);
}
void GuiManager::HideTooltip(GuiWidget* widget)
{
    g_tooltipsCounter.Count();
    g_counter.Count();
    gui::ManagerHide(this, widget);
}
float GuiManager::GetTooltipDelay()
{
    g_tooltipsCounter.Count();
    g_counter.Count();
    return gui_widgets::TooltipDelay(*this);
}

// Only the low byte of the argument is kept.
void GuiWidget::SetVisibleBase(uint32_t visible)
{
    g_counter.Count();
    gui::RunSetVisible(this, static_cast<uint8_t>(visible));
}
void GuiWidget::SetOriginBase(int dx, int dy)
{
    g_counter.Count();
    gui_widgets::SetOrigin(*this, dx, dy);
}
void GuiWidget::TranslateBase(int dx, int dy)
{
    g_counter.Count();
    gui_widgets::Translate(*this, dx, dy);
}
void GuiWidget::SetPosition(int x, int y)
{
    g_counter.Count();
    gui_widgets::SetPosition(*this, x, y);
}
int32_t* GuiWidget::GetBounds(int32_t* out)
{
    g_counter.Count();
    const gui_widgets::Rect bounds = gui_widgets::GetBounds(*this);
    out[0] = bounds.x0;
    out[1] = bounds.y0;
    out[2] = bounds.x1;
    out[3] = bounds.y1;
    return out;
}
bool GuiWidget::HitTestBase(int x, int y)
{
    g_counter.Count();
    return gui_widgets::HitTest(*this, x, y);
}
void GuiWidget::InvalidateBase()
{
    g_counter.Count();
    gui_widgets::Invalidate(*this);
}
// Tooltips: the tooltip counter counts the call; SetTooltip and HideTooltip also count ui.gui-widgets.
bool GuiWidget::TrackTooltip(int x, int y, int state)
{
    g_tooltipsCounter.Count();
    gui::TooltipEnv env;
    return gui_widgets::TrackTooltip(*this, x, y, state, env);
}
bool GuiWidget::HideTooltipOnClick(int state)
{
    g_tooltipsCounter.Count();
    gui::TooltipEnv env;
    return gui_widgets::HideTooltipOnClick(*this, state, env);
}
void GuiWidget::SetTooltipBase(const char* text)
{
    g_tooltipsCounter.Count();
    g_counter.Count();
    gui::RunSetTooltip(this, text);
}
void GuiWidget::HideTooltip()
{
    g_tooltipsCounter.Count();
    g_counter.Count();
    gui::TooltipEnv env;
    env.Hide(*this);
}
bool GuiWidget::ClickTooltip(int, int, int state)
{
    return HideTooltipOnClick(state);
}
} // namespace obl::ui

namespace obl::gui {} // namespace obl::gui

namespace obl::ui {
void GuiWidget::VisibleInvalidate(uint32_t visible)
{
    gui::g_treeCounter.Count();
    gui::g_counter.Count();
    gui::WidgetTreeEnv env;
    gui_widgets::SetVisibleInvalidate(*this, visible, env);
}
bool GuiContainer::ChildrenMouseLeft(int x, int y, int state)
{
    gui::g_treeCounter.Count();
    gui::g_counter.Count();
    gui::WidgetTreeEnv env;
    return gui_widgets::ContainerOnMouseLeft(*this, x, y, state, env);
}
void GuiContainer::ChildrenDrawOverlay()
{
    gui::g_treeCounter.Count();
    gui::g_counter.Count();
    gui::WidgetTreeEnv env;
    gui_widgets::ContainerDrawOverlay(*this, env);
}
} // namespace obl::ui

namespace obl::ui {

void GuiWidgetListNode::DestroyWidget()
{
    gui::g_treeCounter.Count();
    gui::g_counter.Count();
    gui::WidgetTreeEnv env;
    gui_widgets::DestroyWidget(*this, env);
}

} // namespace obl::ui
