#include "core/text16.h"
#include "game/ui/renderer_api.h"
#include "game/ui/text_renderer.h"
#include "gui_tooltip.h"
#include "game/ui/gui_records.h"
#include "gui_lists.h"
#include "gui_manager_frame.h"
#include "gui_widgets.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/game/game_accessors.h"
#include "gui_containers.h"
#include "game/game/game.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/renderer_types.h"
#include "core/call_counter.h"
#include <cwchar>
#include "game/ui/text_table.h"

namespace obl::gui {
namespace {
using namespace gui_records;
using Tooltip = ui::GuiTooltip;
using Text = ui::GuiText;
using Entry = ui::GuiImageListEntry;
using Renderer = ui::TextRenderer;

using DialogNode = ui::GuiManagerDialogNode;
using DialogEntry = ui::GuiManagerDialogEntry;

test::CallCounter g_counter("ui.gui-records");
void CountCall()
{
    g_counter.Count();
}

ui::GuiManager* Gui()
{
    return game::GetGuiManager();
}
Renderer* FontRenderer(int font)
{
    return Gui()->m_textRenderers[font];
}

struct GameEnv {
    void* Allocate(size_t bytes) { return ::operator new(bytes); }
    void DetachDialog(ui::GuiDialogData* dialog) { (static_cast<ui::GuiWidget*>(dialog))->SetVisible(0); }
    void ReleaseSurface(int surface) { obl::ui::renderer::ReleaseSurface(game::GetRenderer(), surface); }
    void DrawCredit(ui::GuiWidget* widget) { widget->DrawOverlay(); }
    const Char16* Lookup(const char* key) { return ui::text_table::Lookup(key); }
    uint32_t MeasureWidth(int font, const Char16* text) { return ui::font_text::MeasureWidth(FontRenderer(font), text); }
    uint32_t MeasureWrappedWidth(int font, const Char16* text, uint32_t maxWidth, uint32_t startX)
    {
        return ui::font_text::MeasureWrappedWidth(FontRenderer(font), text, maxWidth, startX);
    }
    int CountLines(int font, const Char16* text, uint32_t width, uint32_t startX)
    {
        return ui::font_text::CountLines(FontRenderer(font), text, width, startX);
    }
    uint32_t ScreenWidth() { return static_cast<uint32_t>(game::GetRenderer()->m_width); }
    uint32_t ScreenHeight() { return static_cast<uint32_t>(game::GetRenderer()->m_height); }
    void ConstructWidget(ui::GuiTextData* w, int x0, int y0, int x1, int y1) { new (static_cast<void*>(w)) Text(x0, y0, x1, y1); }
    Char16* CopyText(const Char16* text)
    {
        auto* copy = static_cast<Char16*>(::operator new(text16::Length(text) * 2 + 2));
        text16::Copy(copy, text);
        return copy;
    }
    void Free(void* block) { ::operator delete(block); }
    void DrawLoadingBar(float progress) { obl::ui::renderer::DrawLoadingBar(game::GetRenderer(), progress); }
    void* NewBacking() { return container::ConstructMainItem(NewGuiRecord<ui::GuiMainItem>(), 0, 0, 0, 0, kTooltipBackingVariant); }
    // The backing is a GuiMainItem; the tooltip calls its DeletingDestruct, Draw and Invalidate.
    static ui::GuiWidget& Backing(void* backing) { return *static_cast<ui::GuiWidget*>(backing); }
    void DeleteBacking(void* backing) { gui::DestroyWidget(Backing(backing)); }
    void InvalidateBacking(void* backing) { Backing(backing).Invalidate(); }
    void DrawBacking(void* backing) { Backing(backing).Draw(); }
    void SetAlign(int font, int align) { ui::font_text::SetAlign(FontRenderer(font), align); }
    void SetOutlined(int font, int outlined) { ui::font_text::SetOutlined(FontRenderer(font), static_cast<char>(outlined)); }
    void SetColor(int font, uint32_t rgb) { ui::font_text::SetColor(FontRenderer(font), rgb); }
    void SetRect(int font, int x, int y, int w, int h) { ui::font_text::SetRect(FontRenderer(font), x, y, w, h); }
    void DrawWrapped(int font, int x, int y, const Char16* text) { ui::font_text::DrawWrapped(FontRenderer(font), x, y, text); }
};

} // namespace

void LoadingBarSetProgress(float progress)
{
    CountCall();
    GameEnv env;
    SetLoadingProgress(env, progress);
}
ui::GuiScriptNode* GuiListNodeConstruct(ui::GuiScriptNode* node, ui::GuiWidget* widget)
{
    CountCall();
    return ConstructListNode(node, widget);
}
Text* GuiTextConstructLiteral(Text* label, int x0, int y0, int x1, int y1, const Char16* text, int font, uint32_t color, uint32_t flags)
{
    CountCall();
    GameEnv env;
    return static_cast<Text*>(ConstructLiteralText(env, label, x0, y0, x1, y1, text, font, color, flags));
}
DialogNode* GuiAllocDialogNode(DialogEntry** entry)
{
    CountCall();
    GameEnv env;
    return AllocDialogNode(env, entry);
}
ui::TickerList* TickerListConstruct(ui::TickerList* ticker)
{
    CountCall();
    return ConstructTicker(ticker);
}

} // namespace obl::gui

namespace obl::ui {
bool GuiWidget::IsVisible()
{
    gui::CountCall();
    return gui::WidgetIsVisible(*this);
}
bool GuiWidget::IsHovered()
{
    gui::CountCall();
    return gui::WidgetIsHovered(*this);
}
bool GuiWidget::IsHeld()
{
    gui::CountCall();
    return gui::WidgetIsHeld(*this);
}
bool GuiWidget::WasClicked()
{
    gui::CountCall();
    return gui::WidgetWasClicked(*this);
}
} // namespace obl::ui

namespace obl::gui {

// The OnMouseWheel of every GUI class that does not use the wheel (gui_tooltip.h).
bool GuiWidgetOnMouseWheel(ui::GuiWidget*, int, int, int)
{
    CountCall();
    return WidgetOnMouseWheel();
}

void CreditsDraw(ui::GuiCredits* credits)
{
    CountCall();
    GameEnv env;
    DrawCredits(env, *credits);
}

void SetProgressVisibleUnlessSandbox(void* gui)
{
    if (!game::GetGame()->m_sandbox) static_cast<ui::GuiManager*>(gui)->m_progressVisible = 1;
}
void UpdateGuiBarHeight(void* gui)
{
    auto* manager = static_cast<ui::GuiManager*>(gui);
    if (manager->m_mode == 6) return;
    const bool wide = static_cast<uint32_t>(static_cast<ui::Renderer*>(manager->m_renderer)->m_width) >= 800;
    manager->m_barHeight = wide ? 94 : 75;
    obl::ui::renderer::SetGuiBarHeight(manager->m_renderer, wide ? 88 : 70);
}

Entry* GuiImageListEntryDelete(Entry* entry, uint8_t flags)
{
    CountCall();
    GameEnv env;
    return DeleteImageListEntry(env, entry, flags);
}

} // namespace obl::gui

namespace obl::ui {
GuiTooltip* GuiTooltip::Construct(int font)
{
    gui::CountCall();
    gui::GameEnv env;
    return gui_records::ConstructTooltip(env, this, font);
}
void GuiTooltip::Destruct()
{
    gui::CountCall();
    gui::GameEnv env;
    gui_records::DestructTooltip(env, *this);
}
void GuiTooltip::SetText(const char* key)
{
    gui::CountCall();
    gui::GameEnv env;
    gui_records::TooltipSetText(env, *this, key);
}
void GuiTooltip::Show(int x, int y)
{
    gui::CountCall();
    gui::GameEnv env;
    gui_records::TooltipShow(env, *this, x, y);
}
void GuiTooltip::Hide()
{
    gui::CountCall();
    gui_records::HideTooltip(*this);
}
void GuiTooltip::Draw()
{
    gui::CountCall();
    gui::GameEnv env;
    gui_records::DrawTooltip(env, *this);
}
} // namespace obl::ui
