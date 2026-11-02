#pragma once

#include "core/text16.h"
#include "core/ascii_case.h"
#include "game/ui/gui_lists.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/sprite.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace obl::gui_combo {

using List = ui::GuiListData;
using Item = ui::GuiListItem;
using Widget = ui::GuiWidget;
using gui_lists::Add;
using gui_lists::kScrollBarId;
using gui_lists::kSmallBlockBytes;
using gui_lists::Mul;
using gui_lists::StorageBytes;
using gui_lists::Sub;

inline constexpr int32_t kListType = 2;
inline constexpr int32_t kContextSlot = 7; // GuiManager::SetContext index cleared when the list goes away or hides
inline constexpr int32_t kSeparator = -1; // item value of a separator line
inline constexpr int32_t kBoxVariant = 1;
inline constexpr int32_t kPanelDownVariant = 2;
inline constexpr int32_t kPanelUpVariant = 3;
inline constexpr int32_t kBarInset = 0xf; // width of the scroll bar inside the panel
inline constexpr int32_t kPanelMargin = 2;

inline constexpr const char* kKindNames[] = {"GuestFilter", "ResolutionList", "CellPaint", "Scenario", "BeerList", "TestList",
        "BuildingFilter", "StaffFilter", "StaffHire", "EventType0", "EventType1", "EventType2", "CocktailList"};
inline constexpr int kKindCount = static_cast<int>(sizeof(kKindNames) / sizeof(kKindNames[0]));

inline uint32_t ItemCount(const List& list)
{
    return static_cast<uint32_t>(static_cast<int32_t>(list.m_itemsEnd - list.m_itemsBegin));
}

// One item: the record and its text are freed. The end is read once by the callers.
template<class Env> void FreeItem(Item* item, Env& env)
{
    if (!item) return;
    if (item->m_text) env.Free(item->m_text);
    env.Free(item);
}

template<class Env> void FreeItems(List& list, Env& env)
{
    Item** const end = list.m_itemsEnd;
    for (Item** it = list.m_itemsBegin; it != end; ++it) FreeItem(*it, env);
}

// ClearItems: the vector keeps its storage and the selection index stays.
template<class Env> void ClearItems(List& list, Env& env)
{
    FreeItems(list, env);
    list.m_itemsEnd = list.m_itemsBegin;
}

// The sequence three methods share (Destruct, SetVisible(false), OnMouse): bar and panel are deleted, a list that was open redraws.
template<class Env> void CloseDropdown(List& list, Env& env)
{
    if (list.m_scroll) {
        env.DeleteWidget(*list.m_scroll);
        list.m_scroll = nullptr;
    }
    if (list.m_panel) env.DeleteWidget(*list.m_panel);
    const bool wasOpen = list.m_state != 0;
    list.m_panel = nullptr;
    if (wasOpen) env.Invalidate(list);
    list.m_state = 0;
}

// Translate / SetOrigin: the base first, then panel, bar, button and box (the panel and bar only while they exist).
template<class Env> void Translate(List& list, int32_t dx, int32_t dy, Env& env)
{
    env.BaseTranslate(list, dx, dy);
    if (list.m_panel) env.Translate(*list.m_panel, dx, dy);
    if (list.m_scroll) env.Translate(*list.m_scroll, dx, dy);
    env.Translate(*list.m_button, dx, dy);
    env.Translate(*list.m_box, dx, dy);
}

template<class Env> void SetOrigin(List& list, int32_t dx, int32_t dy, Env& env)
{
    env.BaseSetOrigin(list, dx, dy);
    if (list.m_panel) env.SetOrigin(*list.m_panel, dx, dy);
    if (list.m_scroll) env.SetOrigin(*list.m_scroll, dx, dy);
    env.SetOrigin(*list.m_button, dx, dy);
    env.SetOrigin(*list.m_box, dx, dy);
}

// Invalidate: the virtual redraw request.
template<class Env> void Invalidate(List& list, Env& env)
{
    env.BaseInvalidate(list);
    env.Invalidate(*list.m_button);
    env.Invalidate(*list.m_box);
}

// Construct, on a record GuiWidget's constructor placed at `rect`. The kind stays 0 for an unknown name; the button hugs the right
// end of the box (its sprite's width and height come from the style's sprite, which is created only to be measured).
template<class Env>
List* Construct(List& list, const int32_t (&rect)[4], const char* name, uint32_t color, int32_t font, int32_t lines, int32_t style,
        int32_t direction, Env& env)
{
    list.m_style = style;
    list.m_itemsBegin = list.m_itemsEnd = list.m_itemsCap = nullptr;
    list.m_direction = direction;
    list.m_font = font;
    list.m_refresh = 1;
    list.m_type = kListType;
    list.m_scroll = nullptr;
    list.m_color = color;
    list.m_panel = nullptr;
    list.m_selected = 0;
    list.m_lines = static_cast<uint32_t>(lines);
    list.m_rowHeight = static_cast<uint32_t>(Mul(env.LineSize(font), 2));
    for (int kind = 0; kind < kKindCount; ++kind) {
        if (ascii::EqualsNoCase(name, kKindNames[kind])) {
            list.m_kind = kind;
            break;
        }
    }
    list.m_scrollTop = 0;
    list.m_itemsEnd = list.m_itemsBegin;
    const char* graphic = env.SpriteName(style);
    ui::SpriteInstance* sprite = env.CreateSprite(graphic);
    const ui::SpriteAnim& frame = *sprite->m_sprite->m_anims[sprite->m_animIndex];
    const int32_t buttonX0 = Sub(rect[2], frame.m_width);
    const int32_t buttonY1 = Add(static_cast<int32_t>(frame.m_height), rect[1]);
    env.ReleaseSprite(sprite);
    void* memory = env.template NewRecord<ui::GuiButtonData>();
    list.m_button = memory
            ? env.ConstructButton(memory, buttonX0, rect[1], rect[2], buttonY1, graphic, env.NoText(), env.NoAction(), env.NoAction())
            : nullptr;
    memory = env.template NewRecord<ui::GuiMainItemData>();
    auto* box = static_cast<ui::GuiMainItemData*>(
            memory ? env.ConstructMainItem(memory, rect[0], rect[1], buttonX0, rect[3], kBoxVariant) : nullptr);
    list.m_box = box;
    box->m_index = list.m_style;
    return &list;
}

template<class Env> void Destruct(List& list, Env& env)
{
    env.BaseSetVisible(list, 0);
    CloseDropdown(list, env);
    FreeItems(list, env);
    list.m_itemsEnd = list.m_itemsBegin;
    env.SetContext(kContextSlot, 0);
    env.SetVisible(*list.m_button, 0);
    if (list.m_scroll) env.DeleteWidget(*list.m_scroll);
    if (list.m_panel) env.DeleteWidget(*list.m_panel);
    if (list.m_button) env.DeleteWidget(*list.m_button);
    if (list.m_box) env.DeleteWidget(*list.m_box);
    FreeItems(list, env);
    list.m_itemsEnd = list.m_itemsBegin;
    if (list.m_itemsBegin) {
        const uint32_t bytes = StorageBytes(list.m_itemsBegin, list.m_itemsCap);
        if (static_cast<size_t>(list.m_itemsCap - list.m_itemsBegin) > kSmallBlockBytes / 4)
            env.Free(list.m_itemsBegin);
        else
            env.DeallocateSmall(list.m_itemsBegin, bytes);
    }
}

// SetVisible: the argument reaches the base and the button as the whole 32-bit value the caller passed. Showing asks for a refill and
// a redraw; hiding closes the drop-down, empties the item list and clears GUI context entry 7 (kContextSlot).
template<class Env> void SetVisible(List& list, uint32_t visible, Env& env)
{
    env.BaseSetVisible(list, visible);
    if (static_cast<uint8_t>(visible)) {
        list.m_refresh = 1;
        env.Invalidate(list);
        env.SetVisible(*list.m_button, visible);
        return;
    }
    CloseDropdown(list, env);
    FreeItems(list, env);
    list.m_itemsEnd = list.m_itemsBegin;
    env.SetContext(kContextSlot, 0);
    env.SetVisible(*list.m_button, visible);
}

// AddItem: a separator (value -1) has no text; otherwise the key is looked up and the text copied. The vector grows by
// max(size, 1) elements.
template<class Env> void AddItem(List& list, const char* key, int32_t value, Env& env)
{
    Item* item;
    if (value == kSeparator) {
        item = env.template NewRecord<Item>();
        if (item) {
            item->m_text = nullptr;
            item->m_value = kSeparator;
        }
    } else {
        const Char16* text = env.TextLookup(key);
        item = env.template NewRecord<Item>();
        if (item) {
            item->m_text = nullptr;
            if (text) {
                item->m_text = static_cast<Char16*>(env.Alloc((text16::Length(text) + 1) * sizeof(Char16)));
                text16::Copy(item->m_text, text);
            }
            item->m_value = value;
        }
    }
    gui_lists::AppendWord(list.m_itemsBegin, list.m_itemsEnd, list.m_itemsCap, item, env);
}

// AddWideItem: AddItem with the text given instead of a key (the scenario names and the number list). The text is
// copied; a null text leaves the item without one.
template<class Env> void AddWideItem(List& list, const Char16* text, int32_t value, Env& env)
{
    Item* item = env.template NewRecord<Item>();
    if (item) {
        item->m_text = nullptr;
        if (text) {
            item->m_text = static_cast<Char16*>(env.Alloc((text16::Length(text) + 1) * sizeof(Char16)));
            text16::Copy(item->m_text, text);
        }
        item->m_value = value;
    }
    gui_lists::AppendWord(list.m_itemsBegin, list.m_itemsEnd, list.m_itemsCap, item, env);
}

// OnMouseWheel. Closed: a wheel over the box steps the choice (delta < 0 forward, otherwise back) over separators, tells
// the owner through OnItemSelected and redraws. Open with a bar: a cursor over the bar or panel hands the wheel to the bar.
template<class Env> uint8_t OnMouseWheel(List& list, int32_t x, int32_t y, int32_t delta, Env& env)
{
    if (list.m_state == 0) {
        if (!env.HitTest(*list.m_box, x, y)) return 0;
        uint32_t selected = list.m_selected;
        if (delta < 0) {
            if (selected < static_cast<uint32_t>(static_cast<int32_t>(ItemCount(list)) - 1)) {
                ++selected;
                Item** at = list.m_itemsBegin + selected;
                int32_t value = (*at)->m_value;
                while (value == kSeparator && selected < static_cast<uint32_t>(static_cast<int32_t>(ItemCount(list)) - 1)) {
                    value = at[1]->m_value;
                    ++at;
                    ++selected;
                }
            }
        } else if (selected != 0) {
            --selected;
            Item** at = list.m_itemsBegin + selected;
            int32_t value = (*at)->m_value;
            while (value == kSeparator && selected != 0) {
                value = at[-1]->m_value;
                --at;
                --selected;
            }
        }
        list.m_selected = selected;
        env.OnItemSelected(list, list.m_itemsBegin[selected]);
        env.Invalidate(list);
        return 1;
    }
    if (list.m_state == 1 && list.m_scroll && (env.HitTest(*list.m_scroll, x, y) || env.HitTest(*list.m_panel, x, y)))
        return env.OnMouseWheel(*list.m_scroll, x, y, delta);
    return 0;
}

template<class Env> uint32_t RowAt(List& list, int32_t y, Env& env)
{
    int32_t out[4];
    const int32_t* bounds = env.Bounds(*list.m_panel, out);
    return static_cast<uint32_t>(Sub(y, bounds[1])) / list.m_rowHeight + list.m_scrollTop;
}

// Opening (state 0 -> 1, inside OnMouse): the window starts at the chosen item; with more items than lines a bar is built along the panel
// (id 3, 15 pixels wide) and the first row is whatever the bar clamped it to; the panel hangs below the box (variant 2) or above it (variant 3)
// and is as tall as the shown rows plus 2.
template<class Env> void Open(List& list, Env& env)
{
    if (list.m_state != 1) env.Invalidate(list);
    list.m_state = 1;
    uint32_t shown;
    if (ItemCount(list) > list.m_lines) {
        int32_t out[4];
        const int32_t barX0 = Sub(env.Bounds(*list.m_box, out)[2], kBarInset);
        const int32_t barX1 = env.Bounds(*list.m_box, out)[2];
        int32_t barY0, barY1;
        if (list.m_direction == 0) {
            barY0 = list.m_y1;
            barY1 = Add(Mul(static_cast<int32_t>(list.m_rowHeight), static_cast<int32_t>(list.m_lines)), barY0);
        } else {
            barY1 = Sub(list.m_y1, static_cast<int32_t>(list.m_rowHeight));
            barY0 = Sub(barY1, Mul(static_cast<int32_t>(list.m_rowHeight), static_cast<int32_t>(list.m_lines)));
        }
        list.m_scrollTop = list.m_selected;
        void* memory = env.template NewRecord<ui::GuiScrollBarData>();
        list.m_scroll = memory
                ? env.ConstructScroll(memory, barX0, barY0, barX1, barY1, list.m_scrollTop, ItemCount(list), list.m_lines, kScrollBarId)
                : nullptr;
        list.m_scrollTop = list.m_scroll->m_top;
        shown = list.m_lines;
    } else {
        shown = ItemCount(list);
    }
    int32_t out[4];
    const int32_t x0 = env.Bounds(*list.m_box, out)[0];
    const int32_t x1 = env.Bounds(*list.m_box, out)[2];
    void* memory = env.template NewRecord<ui::GuiMainItemData>();
    const int32_t span = Mul(static_cast<int32_t>(list.m_rowHeight), static_cast<int32_t>(shown));
    if (list.m_direction == 0) {
        const int32_t y0 = list.m_y1;
        list.m_panel = memory ? env.ConstructMainItem(memory, x0, y0, x1, Add(Add(span, y0), kPanelMargin), kPanelDownVariant) : nullptr;
    } else {
        const int32_t y1 = list.m_y0;
        list.m_panel = memory ? env.ConstructMainItem(memory, x0, Sub(Sub(y1, span), kPanelMargin), x1, y1, kPanelUpVariant) : nullptr;
    }
    list.m_hover = list.m_selected;
}

// OnMouse, the list's OnMouseLeft (`state` 0 move, 1 press). Returns the raw byte: 1 when a press chose an item or closed the open drop-down over
// the box, the bar's own answer when the bar consumed the event, 0 otherwise. Closed: a press over the box opens it. Open: the panel sees
// every event; a move updates the hovered row; a press on an item (not a separator) selects it, informs the owner and closes; a press
// anywhere else closes (a press over the box itself also reports 1).
template<class Env> uint8_t OnMouse(List& list, int32_t x, int32_t y, int32_t state, Env& env)
{
    uint8_t result = 0;
    env.TrackTooltip(list, x, y, state);
    env.OnMouseLeft(*list.m_button, x, y, state);
    list.m_hovered = env.HitTest(list, x, y);
    if (list.m_state == 0) {
        if (env.IsHovered(list) && state == 1) Open(list, env);
        return result;
    }
    if (list.m_state != 1) return result;
    env.OnMouseLeft(*list.m_panel, x, y, state);
    if (state == 0) {
        if (!list.m_scroll || (!env.HitTest(*list.m_scroll, x, y) && !list.m_scroll->m_dragging)) {
            if (env.IsHovered(*list.m_panel)) list.m_hover = RowAt(list, y, env);
        }
    }
    if (list.m_scroll) {
        result = env.OnMouseLeft(*list.m_scroll, x, y, state);
        if (result) return result;
    }
    if (env.IsHovered(list) && state == 1) {
        CloseDropdown(list, env);
        return 1;
    }
    if (env.WasClicked(*list.m_panel)) {
        list.m_panel->m_clicked = 0;
        uint32_t row = RowAt(list, y, env);
        const uint32_t count = ItemCount(list);
        if (row >= count) row = count - 1;
        Item* item = list.m_itemsBegin[row];
        if (item->m_value == kSeparator) return result;
        list.m_selected = row;
        env.OnItemSelected(list, item);
        CloseDropdown(list, env);
        return 1;
    }
    if (env.IsHovered(list)) return result;
    if (state != 1) return result;
    CloseDropdown(list, env);
    return result;
}

// Draw: while dirty the closed box is redrawn and the chosen item's text is written in it (6 pixels in from the left edge);
// the button draws every time.
template<class Env> void Draw(List& list, Env& env)
{
    if (list.m_dirty) {
        env.Invalidate(*list.m_box);
        env.Draw(*list.m_box);
        if (list.m_itemsBegin != list.m_itemsEnd) {
            int32_t out[4];
            const int32_t* b = env.Bounds(*list.m_box, out);
            const int32_t x0 = b[0], y0 = b[1], x1 = b[2], y1 = b[3];
            const Item* item = list.m_itemsBegin[list.m_selected];
            env.TextAlign(list.m_font, 0);
            env.TextOutlined(list.m_font, 0);
            env.TextColor(list.m_font, list.m_color);
            env.TextRect(list.m_font, Add(x0, 6), y0, Sub(x1, x0), Sub(y1, y0));
            env.TextDraw(list.m_font, 0, 1, item->m_text);
        }
        list.m_dirty = 0;
    }
    env.Draw(*list.m_button);
}

// DrawRowHighlight: the thin separator line across the box's width (less the bar and a margin), halfway down the row at `y`.
template<class Env> void DrawRowHighlight(List& list, int32_t y, Env& env)
{
    int32_t out[4];
    const int32_t right = env.Bounds(*list.m_box, out)[2];
    int32_t out2[4];
    const int32_t left = env.Bounds(*list.m_box, out2)[0];
    int32_t width = Sub(Sub(right, left), 8);
    if (list.m_scroll) width = Sub(width, kBarInset);
    int32_t out3[4];
    int32_t rect[4];
    rect[0] = Add(env.Bounds(*list.m_box, out3)[0], 4);
    rect[1] = Add(static_cast<int32_t>(list.m_rowHeight >> 1), y);
    rect[2] = width;
    rect[3] = 2;
    env.FillSeparator(rect);
}

// DrawOverlay: the open panel is redrawn (invalidated, then drawn) and every item of the window `scrollTop.. scrollTop +
// lines` gets a row: the hovered one a blue band with inverted text, the others plain text, separators a thin line. Then the bar and the
// button draw their overlays.
template<class Env> void DrawOverlay(List& list, Env& env)
{
    if (list.m_panel) {
        const int32_t font = list.m_font;
        int32_t offset = 0;
        env.TextAlign(font, 0);
        int32_t out[4];
        const int32_t* panel = env.Bounds(*list.m_panel, out);
        const int32_t panelX0 = panel[0], panelY0 = panel[1], panelX1 = panel[2];
        env.Invalidate(*list.m_panel);
        env.Draw(*list.m_panel);
        if (ItemCount(list) != 0) {
            uint32_t index = 0;
            do {
                Item* item = list.m_itemsBegin[index];
                if (index >= list.m_scrollTop && index < list.m_lines + list.m_scrollTop) {
                    const int32_t y = Add(offset, panelY0);
                    int32_t box[4];
                    if (index == list.m_hover) {
                        if (item->m_value == kSeparator) {
                            DrawRowHighlight(list, y, env);
                        } else {
                            int32_t rect[4];
                            rect[0] = Add(env.Bounds(*list.m_box, box)[0], kPanelMargin);
                            rect[1] = y;
                            int32_t box2[4];
                            rect[2] = Sub(env.Bounds(*list.m_box, box2)[2], kPanelMargin);
                            rect[3] = Add(Add(static_cast<int32_t>(list.m_rowHeight), panelY0), offset);
                            env.FillHover(rect);
                            env.TextColor(font, ~list.m_color);
                            int32_t box3[4];
                            env.TextRect(font, env.Bounds(*list.m_box, box3)[0], y, Sub(panelX1, panelX0),
                                    static_cast<int32_t>(list.m_rowHeight));
                            env.TextDraw(font, 4, 1, item->m_text);
                        }
                    } else if (item->m_value == kSeparator) {
                        DrawRowHighlight(list, y, env);
                    } else {
                        env.TextColor(font, list.m_color);
                        env.TextRect(
                                font, env.Bounds(*list.m_box, box)[0], y, Sub(panelX1, panelX0), static_cast<int32_t>(list.m_rowHeight));
                        env.TextDraw(font, 4, 1, item->m_text);
                    }
                    offset = Add(offset, static_cast<int32_t>(list.m_rowHeight));
                }
                ++index;
            } while (index < ItemCount(list));
        }
    }
    if (list.m_scroll) env.DrawOverlay(*list.m_scroll);
    env.DrawOverlay(*list.m_button);
}

} // namespace obl::gui_combo
