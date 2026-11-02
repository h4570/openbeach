#pragma once

#include "core/char16.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_lists_types.h"
#include "game/ui/gui_script_types.h"

#include <cstdint>

// The GUI's small records: tooltip text/placement, the literal-text widget constructor, the
// delete helpers of image-list entries / count labels / list items and the loading-bar forwarder. Heap and game calls go through an `Env`, so the
// unit tests run this code on plain memory. 32-bit operands wrap.
//   Env (tooltip)   const wchar_t* Lookup(const char* key)                     the key's text in the loaded text table
//                   uint32_t MeasureWidth(int font, const wchar_t*) / MeasureWrappedWidth(int font, const wchar_t*, uint32_t maxWidth, uint32_t startX)
//                   int CountLines(int font, const wchar_t*, uint32_t width, uint32_t startX) / uint32_t ScreenWidth() / uint32_t ScreenHeight()
//   Env (text)      void ConstructWidget(GuiTextData*, int x0, int y0, int x1, int y1) / wchar_t* CopyText(const wchar_t*)
//   Env (delete)    void Free(void*)       Env (loading bar)   void DrawLoadingBar(float)
namespace obl::gui_records {
using gui_widgets::Add;
using gui_widgets::Sub;

inline constexpr int32_t kTextWidgetType = 12;
inline constexpr uint32_t kTooltipWrapWidth = 0xa0; // text wider than this wraps
inline constexpr uint32_t kTooltipWrapStart = 5;
inline constexpr int32_t kTooltipLineHeight = 12;
inline constexpr int32_t kTooltipPadding = 10; // box = text + padding horizontally
inline constexpr int32_t kTooltipBelowCursor = 0x19; // the box sits this far under the pointer, or above it when it would leave the screen

template<class Env> void TooltipSetText(Env& env, ui::GuiTooltip& tooltip, const char* key)
{
    tooltip.m_text = env.Lookup(key);
    uint32_t width = env.MeasureWidth(tooltip.m_font, tooltip.m_text);
    if (width > kTooltipWrapWidth) {
        width = env.MeasureWrappedWidth(tooltip.m_font, tooltip.m_text, kTooltipWrapWidth, kTooltipWrapStart);
        tooltip.m_textWidth = Add(static_cast<int32_t>(width), kTooltipPadding);
        const int32_t lines = env.CountLines(tooltip.m_font, tooltip.m_text, static_cast<uint32_t>(tooltip.m_textWidth), kTooltipWrapStart);
        tooltip.m_textHeight = static_cast<int32_t>(static_cast<uint32_t>(lines) * kTooltipLineHeight);
        tooltip.m_boxWidth = tooltip.m_textWidth;
        tooltip.m_boxHeight = Add(tooltip.m_textHeight, 6);
        return;
    }
    tooltip.m_textWidth = static_cast<int32_t>(width);
    tooltip.m_textHeight = 0xf;
    tooltip.m_boxWidth = Add(static_cast<int32_t>(width), kTooltipPadding);
    tooltip.m_boxHeight = 0x15;
}

template<class Env> void TooltipShow(Env& env, ui::GuiTooltip& tooltip, int32_t x, int32_t y)
{
    if (tooltip.m_visible) return;
    tooltip.m_visible = 1;
    tooltip.m_x = x;
    tooltip.m_y = Add(y, kTooltipBelowCursor);
    if (env.ScreenHeight() < static_cast<uint32_t>(Add(tooltip.m_y, tooltip.m_boxHeight)))
        tooltip.m_y = Sub(Sub(y, tooltip.m_boxHeight), 2);
    if (env.ScreenWidth() < static_cast<uint32_t>(Add(tooltip.m_boxWidth, x)))
        tooltip.m_x = Sub(static_cast<int32_t>(env.ScreenWidth()), tooltip.m_boxWidth);
    ui::GuiWidget& backing = *tooltip.m_backing;
    backing.m_x0 = tooltip.m_x;
    backing.m_y0 = tooltip.m_y;
    backing.m_x1 = Add(tooltip.m_boxWidth, tooltip.m_x);
    backing.m_y1 = Add(tooltip.m_boxHeight, tooltip.m_y);
}

// A text widget that owns a copy of `text` (the key-based constructor borrows from the text table instead).
template<class Env>
ui::GuiTextData* ConstructLiteralText(
        Env& env, ui::GuiTextData* widget, int x0, int y0, int x1, int y1, const Char16* text, int font, uint32_t color, uint32_t flags)
{
    env.ConstructWidget(widget, x0, y0, x1, y1);
    widget->m_text = nullptr;
    widget->m_ownedText = nullptr;
    widget->m_drawOnSurface = 1;
    widget->m_ownedText = env.CopyText(text);
    widget->m_font = font;
    widget->m_alignFlags = flags;
    widget->m_color = color;
    widget->m_type = kTextWidgetType;
    return widget;
}

template<class Env> ui::GuiImageListEntry* DeleteImageListEntry(Env& env, ui::GuiImageListEntry* entry, uint8_t flags)
{
    if (entry->m_tooltip) env.Free(entry->m_tooltip);
    if (flags & 1) env.Free(entry);
    return entry;
}
template<class Env, class Label> Label* DeleteImageListLabel(Env& env, Label* label, uint8_t flags)
{
    if (flags & 1) env.Free(label);
    return label;
}
// Null-tolerant, text first then the item itself.
template<class Env> void FreeListItem(Env& env, ui::GuiListItem* item)
{
    if (!item) return;
    if (item->m_text) env.Free(item->m_text);
    env.Free(item);
}

template<class Env> void SetLoadingProgress(Env& env, float progress)
{
    env.DrawLoadingBar(progress);
}

inline constexpr int kTooltipBackingVariant = 7;
template<class Env> ui::GuiTooltip* ConstructTooltip(Env& env, ui::GuiTooltip* tooltip, int font)
{
    tooltip->m_visible = 0;
    tooltip->m_text = nullptr;
    tooltip->m_font = font;
    tooltip->m_backing = static_cast<ui::GuiWidget*>(env.NewBacking());
    return tooltip;
}
// The backing is destroyed through its virtual DeletingDestruct with flag 1.
template<class Env> void DestructTooltip(Env& env, ui::GuiTooltip& tooltip)
{
    if (tooltip.m_backing) env.DeleteBacking(tooltip.m_backing);
}
// Draws only a shown tooltip: the backing is invalidated and drawn (its virtual Invalidate and Draw), then the text goes on top at (5,3) in the tooltip's font
// with left alignment, no outline and black.
//   Env (draw)   void InvalidateBacking(GuiWidget*) / void DrawBacking(GuiWidget*)
//                void SetAlign(int font, int) / SetOutlined(int font, int) / SetColor(int font, uint32_t) / SetRect(int font, int x, int y, int w, int h)
//                void DrawWrapped(int font, int x, int y, const wchar_t*)
template<class Env> void DrawTooltip(Env& env, ui::GuiTooltip& tooltip)
{
    if (!tooltip.m_visible) return;
    env.InvalidateBacking(tooltip.m_backing);
    env.DrawBacking(tooltip.m_backing);
    env.SetAlign(tooltip.m_font, 0);
    env.SetOutlined(tooltip.m_font, 0);
    env.SetColor(tooltip.m_font, 0);
    env.SetRect(tooltip.m_font, tooltip.m_x, tooltip.m_y, tooltip.m_textWidth, tooltip.m_textHeight);
    env.DrawWrapped(tooltip.m_font, 5, 3, tooltip.m_text);
}
inline void HideTooltip(ui::GuiTooltip& tooltip)
{
    tooltip.m_visible = 0;
}

// One-line GuiWidget accessors of the base class; SelectTab and OnMouseWheel are the base no-ops.
inline bool WidgetIsVisible(const ui::GuiWidget& w)
{
    return w.m_visible != 0;
}
inline bool WidgetIsHovered(const ui::GuiWidget& w)
{
    return w.m_hovered != 0;
}
inline bool WidgetIsHeld(const ui::GuiWidget& w)
{
    return w.m_held != 0;
}
inline bool WidgetWasClicked(const ui::GuiWidget& w)
{
    return w.m_clicked != 0;
}
inline bool WidgetOnMouseWheel()
{
    return false;
}

inline ui::TickerList* ConstructTicker(ui::TickerList* ticker)
{
    ticker->m_listBox = nullptr;
    ticker->m_visible = 0;
    ticker->m_rows = 0;
    return ticker;
}
inline ui::GuiScriptNode* ConstructListNode(ui::GuiScriptNode* node, ui::GuiWidget* widget)
{
    node->m_widget = widget;
    node->m_next = nullptr;
    return node;
}
template<class Env> ui::GuiManagerDialogNode* AllocDialogNode(Env& env, ui::GuiManagerDialogEntry** entry)
{
    auto* node = static_cast<ui::GuiManagerDialogNode*>(env.Allocate(sizeof(ui::GuiManagerDialogNode)));
    node->m_entry = *entry;
    return node;
}
template<class Env> void ReleaseDialogEntry(Env& env, ui::GuiManagerDialogEntry* entry)
{
    if (!entry) return;
    env.DetachDialog(entry->m_dialog);
    env.ReleaseSurface(entry->m_surface);
    env.Free(entry);
}
template<class Env> void DrawCredits(Env& env, ui::GuiCredits& credits)
{
    for (auto* node = credits.m_lines->m_next; node != credits.m_lines; node = node->m_next) env.DrawCredit(node->m_line->m_text);
}
} // namespace obl::gui_records
