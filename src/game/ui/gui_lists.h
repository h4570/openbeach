#pragma once

#include "core/key_code.h"

#include "core/text16.h"
#include "core/pointer_width.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_lists_types.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cwchar>
#include "core/char16.h"

namespace obl::gui_lists {

using Cell = ui::GuiListRowCell;
using ListBox = ui::GuiListBoxData;
using Widget = ui::GuiWidget;
using gui_widgets::Add;
using gui_widgets::Sub;

inline constexpr int32_t kCellButtonType = 0; // widget type of a row button (listbox::SetRowButton)
inline constexpr int32_t kListBoxType = 0x16;
inline constexpr int32_t kCellImageType = 10; // widget type of a row image: DrawOverlay picks its frame by the highlight

inline constexpr uint32_t kScrollBarId = 3; // id UpdateScrollBar gives the bar
inline constexpr uint32_t kSmallBlockBytes = 0x80;
inline constexpr auto kCtrlLeft = input::KeyCode::LeftControl;
inline constexpr auto kCtrlRight = input::KeyCode::RightControl;
inline constexpr float kRepeatDelay = 0.25f; // seconds a held press waits before it counts as a long press (constant)

inline int32_t Mul(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

// ClearSelection: the selection vector keeps its storage, only its end moves.
inline void ClearSelection(ListBox& list)
{
    list.m_selEnd = list.m_selBegin;
}

template<class T, class Env> void AppendWord(T*& begin, T*& end, T*& cap, T value, Env& env)
{
    if (end == cap) {
        const size_t count = begin ? end - begin : 0;
        const size_t capacity = count + std::max(count, size_t(1));
        // T is often a pointer: the slot size is wanted.
        auto* storage = static_cast<T*>(env.AllocWords(capacity * sizeof(T), capacity * 4)); // NOLINT(bugprone-sizeof-expression)
        if (count) std::memmove(storage, begin, count * sizeof(T));
        storage[count] = value;
        if (begin) {
            const uint32_t bytes = static_cast<uint32_t>((cap - begin) * sizeof(T));
            if (static_cast<size_t>(cap - begin) > kSmallBlockBytes / 4)
                env.Free(begin);
            else
                env.DeallocateSmall(begin, bytes);
        }
        begin = storage;
        end = storage + count + 1;
        cap = storage + capacity;
    } else {
        if (end) *end = value;
        ++end;
    }
}

template<class Env> void SelectRow(ListBox& list, uint32_t row, int replace, Env& env)
{
    if (list.m_cellsBegin == list.m_cellsEnd) return;
    const int32_t last = static_cast<int32_t>(list.m_rowCount - 1);
    if (last < static_cast<int32_t>(row)) row = static_cast<uint32_t>(last);
    if (!list.m_multiSelect || static_cast<uint8_t>(replace)) {
        ClearSelection(list);
        AppendWord(list.m_selBegin, list.m_selEnd, list.m_selCap, static_cast<int32_t>(row), env);
        return;
    }
    auto* found = std::find(list.m_selBegin, list.m_selEnd, static_cast<int32_t>(row));
    if (found == list.m_selEnd) {
        AppendWord(list.m_selBegin, list.m_selEnd, list.m_selCap, static_cast<int32_t>(row), env);
    } else if (list.m_selEnd - list.m_selBegin > 1) {
        std::memmove(found, found + 1, (list.m_selEnd - found - 1) * sizeof(int32_t));
        --list.m_selEnd;
    }
    if (list.m_selBegin != list.m_selEnd) std::sort(list.m_selBegin, list.m_selEnd);
}

template<class Env> Cell* NewCell(int32_t row, Env& env)
{
    auto* cell = env.template NewRecord<Cell>();
    cell->m_widget = nullptr;
    cell->m_row = -1;
    cell->m_active = 1;
    cell->m_row = row;
    return cell;
}

template<class Env> void AddImage(ListBox& list, int32_t row, int32_t offset, const char* graphic, Env& env)
{
    Cell* cell = NewCell(row, env);
    int32_t bounds[4];
    const auto* rect = env.Bounds(list, bounds);
    const int32_t x = Add(rect[0], offset), y = Add(rect[1], Mul(list.m_rowHeight, row));
    void* memory = env.template NewRecord<ui::GuiImageData>();
    cell->m_widget = memory ? env.ConstructImage(memory, x, y, graphic) : nullptr;
    AppendWord(list.m_cellsBegin, list.m_cellsEnd, list.m_cellsCap, cell, env);
}

template<class Env> void SetRowButton(ListBox& list, int32_t row, int32_t offset, const char* graphic, int32_t value, Env& env)
{
    for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) {
        Cell& cell = **it;
        if (cell.m_row == row && cell.m_widget->m_type == kCellButtonType) {
            cell.m_value = value;
            cell.m_active = 1;
            return;
        }
    }
    Cell* cell = NewCell(row, env);
    int32_t bounds[4];
    const auto* rect = env.Bounds(list, bounds);
    const int32_t x = Add(rect[0], offset), y = Add(rect[1], Mul(list.m_rowHeight, row));
    void* memory = env.template NewRecord<ui::GuiButtonData>();
    auto* button = static_cast<ui::GuiButtonData*>(memory ? env.ConstructButton(memory, x, y, graphic) : nullptr);
    button->m_repeat = 0;
    env.SetVisible(*button, 1);
    cell->m_widget = button;
    cell->m_value = value;
    AppendWord(list.m_cellsBegin, list.m_cellsEnd, list.m_cellsCap, cell, env);
}

template<class Text, class Env>
void AddText(ListBox& list, int32_t row, int32_t offset, int32_t width, uint32_t unused, uint32_t flags, int32_t font, uint32_t color,
        const Text* text, uint32_t newRow, Env& env)
{
    (void)unused;
    Cell* cell = NewCell(row, env);
    int32_t bounds[4];
    const auto* rect = env.Bounds(list, bounds);
    const int32_t x = Add(rect[0], offset), y = Add(rect[1], Mul(list.m_rowHeight, row));
    Char16 buffer[512];
    if constexpr (sizeof(Text) == 1)
        env.Widen(text, buffer);
    else
        text16::Copy(buffer, text);
    env.Truncate(font, buffer, width);
    void* memory = env.template NewRecord<ui::GuiTextData>();
    auto* widget = static_cast<ui::GuiTextData*>(
            memory ? env.ConstructText(memory, x, y, Add(x, width), Add(y, list.m_rowHeight), buffer, font, color, flags) : nullptr);
    widget->m_drawOnSurface = 0;
    cell->m_widget = widget;
    AppendWord(list.m_cellsBegin, list.m_cellsEnd, list.m_cellsCap, cell, env);
    if (static_cast<uint8_t>(newRow)) ++list.m_rowCount;
    env.UpdateScrollBar(list);
    list.m_slotCount = std::min(list.m_rowCount, static_cast<uint32_t>(list.m_visibleRows));
    env.Invalidate(list);
}

inline int32_t NextSelectedRow(ListBox& list)
{
    list.m_selCursor = Add(list.m_selCursor, 1);
    return list.m_selBegin[list.m_selCursor];
}

// Cells of row `top + slot` are moved to the y of the slot (the x stays). Shared tail of Update, SetScrollTop and EnsureRowVisible.
template<class Env> void PlaceCells(ListBox& list, Env& env)
{
    for (uint32_t slot = 0; slot < list.m_slotCount; ++slot) {
        for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) {
            Cell& cell = **it;
            if (cell.m_row != Add(list.m_scrollTop, static_cast<int32_t>(slot))) continue;
            int32_t bounds[4];
            const int32_t* rect = env.Bounds(*cell.m_widget, bounds);
            env.SetPosition(*cell.m_widget, rect[0], Add(Mul(list.m_rowHeight, static_cast<int32_t>(slot)), list.m_y0));
        }
    }
}

// SetScrollTop.
template<class Env> void SetScrollTop(ListBox& list, int32_t top, Env& env)
{
    if (top < 0) top = 0;
    list.m_scrollTop = top;
    if (list.m_scroll) env.SetTopRow(*list.m_scroll, static_cast<uint32_t>(top));
    PlaceCells(list, env);
}

// EnsureRowVisible: the unsigned comparisons make a "negative" row count as far below the window.
template<class Env> void EnsureRowVisible(ListBox& list, uint32_t row, Env& env)
{
    if (row < static_cast<uint32_t>(list.m_scrollTop)) {
        SetScrollTop(list, static_cast<int32_t>(row), env);
    }
    const uint32_t slots = list.m_slotCount;
    if (row >= slots + static_cast<uint32_t>(list.m_scrollTop)) {
        uint32_t top = row - slots + 1;
        if (slots + top > list.m_rowCount) top = list.m_rowCount - static_cast<uint32_t>(list.m_visibleRows);
        SetScrollTop(list, static_cast<int32_t>(top), env);
    }
}

// Update: the bar moves first and may scroll the list, then every cell and the press timer advance. `dtBits` is the float
// the caller pushed, passed on untouched.
template<class Env> void Update(ListBox& list, uint32_t dtBits, Env& env)
{
    if (list.m_scroll) {
        env.Update(*list.m_scroll, dtBits);
        const int32_t barTop = static_cast<int32_t>(list.m_scroll->m_top);
        if (barTop != list.m_scrollTop) {
            list.m_scrollTop = barTop;
            PlaceCells(list, env);
        }
    }
    for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) env.Update(*(*it)->m_widget, dtBits);
    if (list.m_holding) list.m_holdTime = std::bit_cast<float>(dtBits) + list.m_holdTime;
}

// Draw: the rows are painted by DrawOverlay, only the bar has a surface part.
template<class Env> void Draw(ListBox& list, Env& env)
{
    if (list.m_scroll) env.Draw(*list.m_scroll);
}

// Invalidate.
template<class Env> void Invalidate(ListBox& list, Env& env)
{
    env.BaseInvalidate(list);
    for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) env.Invalidate(*(*it)->m_widget);
}

// Translate: the base moves the rectangle, the bar rectangle is shifted here, bar and cells follow through their own Translate.
template<class Env> void Translate(ListBox& list, int32_t dx, int32_t dy, Env& env)
{
    env.BaseTranslate(list, dx, dy);
    list.m_barY1 = Add(list.m_barY1, dy);
    list.m_barX0 = Add(list.m_barX0, dx);
    list.m_barX1 = Add(list.m_barX1, dx);
    list.m_barY0 = Add(list.m_barY0, dy);
    if (list.m_scroll) env.Translate(*list.m_scroll, dx, dy);
    for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) env.Translate(*(*it)->m_widget, dx, dy);
}

// OnMouseLeft. The bar and the active row buttons see the click first (the first one that answers wins, and the answer is
// the raw byte it returned). Otherwise a press (state 1) inside the list selects the row under the cursor: SelectRow replaces the
// selection (argument 1) unless either Ctrl is down, which toggles the row of a multi-select list (argument 0) and arms nothing.
// Without Ctrl the press arms the repeat timer; a second press within kRepeatDelay sets the one-frame m_repeat flag and disarms it.
template<class Env> uint8_t OnMouseLeft(ListBox& list, int32_t x, int32_t y, int32_t state, Env& env)
{
    uint8_t answer = 0;
    list.m_repeat = 0;
    if (list.m_scroll) answer = env.OnMouseLeft(*list.m_scroll, x, y, state);
    for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) {
        if (answer) return answer;
        Widget& widget = *(*it)->m_widget;
        if (widget.m_type == kCellButtonType && (*it)->m_active) answer = env.OnMouseLeft(widget, x, y, state);
    }
    if (answer) return answer;
    if (!list.m_enabled) return 0;
    if (state != 1 || !env.HitTest(list, x, y)) {
        list.m_holding = 0;
        return 0;
    }
    bool replace = true;
    int32_t rect[4];
    const int32_t* bounds = env.Bounds(list, rect);
    const uint32_t row = static_cast<uint32_t>(Sub(y, bounds[1])) / static_cast<uint32_t>(list.m_rowHeight);
    const int32_t first = Add(static_cast<int32_t>(row), list.m_scrollTop);
    if (env.KeyHeld(kCtrlLeft) || env.KeyHeld(kCtrlRight)) replace = false;
    env.SelectRow(list, static_cast<uint32_t>(first), replace ? 1 : 0);
    if (!replace) {
        list.m_holding = 0;
        return 1;
    }
    if (!list.m_holding) {
        list.m_holding = 1;
        list.m_holdTime = 0.0f;
        return 1;
    }

    if (!(list.m_holdTime >= kRepeatDelay)) {
        list.m_repeat = 1;
        list.m_holding = 0;
    }
    return 1;
}

// OnMouseWheel: only a list with a bar reacts, to a cursor over the list or over the bar; the bar's answer is returned.
template<class Env> uint8_t OnMouseWheel(ListBox& list, int32_t x, int32_t y, int32_t delta, Env& env)
{
    if (!list.m_scroll) return 0;
    if (!env.HitTest(list, x, y) && !env.HitTest(*list.m_scroll, x, y)) return 0;
    return env.OnMouseWheel(*list.m_scroll, x, y, delta);
}

// PollRowButton: the value of the first clicked button in `row`; the click is consumed.
template<class Env> uint8_t PollRowButton(ListBox& list, int32_t row, int32_t* value, Env& env)
{
    uint8_t found = 0;
    for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) {
        if (found) break;
        Cell& cell = **it;
        if (cell.m_row == row && cell.m_widget->m_type == kCellButtonType && env.IsClicked(*cell.m_widget)) {
            *value = cell.m_value;
            found = 1;
            cell.m_widget->m_clicked = 0;
        }
    }
    return found;
}

// Every cell owns its widget (deleted through the virtual destructor) and is itself a 0x10-byte game-heap block.
template<class Env> void DeleteCell(Cell* cell, Env& env)
{
    if (cell) {
        if (cell->m_widget) env.DeleteWidget(*cell->m_widget);
        env.Free(cell);
    }
}

inline uint32_t StorageBytes(const void* begin, const void* cap)
{
    const int32_t bytes = static_cast<int32_t>(static_cast<const char*>(cap) - static_cast<const char*>(begin));
    return static_cast<uint32_t>(bytes >> 2) * 4u;
}

template<class Env>
ListBox* Construct(ListBox& list, const int32_t (&bar)[4], const char* name, int32_t rowHeight, int32_t visibleRows, Env& env)
{
    list.m_barX0 = bar[0];
    list.m_barY0 = bar[1];
    list.m_barX1 = bar[2];
    list.m_barY1 = bar[3];
    list.m_multiSelect = 0;
    list.m_enabled = 1;
    list.m_rowHeight = rowHeight;
    list.m_visibleRows = visibleRows;
    list.m_scrollTop = 0;
    list.m_slotCount = 0;
    list.m_cellsBegin = list.m_cellsEnd = list.m_cellsCap = nullptr;
    list.m_selBegin = list.m_selEnd = list.m_selCap = nullptr;
    list.m_selCursor = 0;
    list.m_scroll = nullptr;
    list.m_holding = 0;
    list.m_repeat = 0;
    list.m_type = kListBoxType;
    const size_t size = std::strlen(name) + 1;
    list.m_name = static_cast<char*>(env.Alloc(size));
    std::memcpy(list.m_name, name, size);
    return &list;
}

template<class Env> void Destruct(ListBox& list, Env& env)
{
    env.Free(list.m_name);
    Cell** const end = list.m_cellsEnd;
    for (Cell** it = list.m_cellsBegin; it != end; ++it) DeleteCell(*it, env);
    list.m_cellsEnd = list.m_cellsBegin;
    list.m_slotCount = 0;
    list.m_scrollTop = 0;
    list.m_rowCount = 0;
    list.m_selEnd = list.m_selBegin;
    if (list.m_scroll) env.DeleteWidget(*list.m_scroll);
    if (list.m_selBegin) env.DeallocateWords(list.m_selBegin, StorageBytes(list.m_selBegin, list.m_selCap));
    if (list.m_cellsBegin) {
        const uint32_t bytes = StorageBytes(list.m_cellsBegin, list.m_cellsCap);
        if (static_cast<size_t>(list.m_cellsCap - list.m_cellsBegin) > kSmallBlockBytes / 4)
            env.Free(list.m_cellsBegin);
        else
            env.DeallocateSmall(list.m_cellsBegin, bytes);
    }
}

// ClearRows: deletes every cell except the ones that hold a button (those stay, deactivated), forgets the scroll position and the
// bar, and asks for a redraw through the virtual Invalidate. The cell vector is edited in place, its end is re-read every pass.
template<class Env> void ClearRows(ListBox& list, Env& env)
{
    Cell** it = list.m_cellsBegin;
    if (it != list.m_cellsEnd) {
        do {
            Cell* cell = *it;
            if (cell->m_widget->m_type == kCellButtonType) {
                ++it;
                cell->m_active = 0;
            } else {
                DeleteCell(cell, env);
                Cell** const next = it + 1;
                if (list.m_cellsEnd != next) std::memmove(it, next, static_cast<size_t>(list.m_cellsEnd - next) * sizeof(Cell*));
                --list.m_cellsEnd;
            }
        } while (it != list.m_cellsEnd);
    }
    list.m_slotCount = 0;
    list.m_scrollTop = 0;
    list.m_rowCount = 0;
    if (list.m_scroll) {
        env.DeleteWidget(*list.m_scroll);
        list.m_scroll = nullptr;
    }
    env.Invalidate(list);
}

// SetVisible: the argument reaches the base and the bar as the whole 32-bit value the caller passed; hiding also deletes every
// cell (buttons too, unlike ClearRows) and resets the row counters, the bar stays.
template<class Env> void SetVisible(ListBox& list, uint32_t visible, Env& env)
{
    env.BaseSetVisible(list, visible);
    if (list.m_scroll) env.SetVisible(*list.m_scroll, visible);
    if (static_cast<uint8_t>(visible) != 0) return;
    Cell** const end = list.m_cellsEnd;
    for (Cell** it = list.m_cellsBegin; it != end; ++it) DeleteCell(*it, env);
    list.m_cellsEnd = list.m_cellsBegin;
    list.m_slotCount = 0;
    list.m_scrollTop = 0;
    list.m_rowCount = 0;
}

// UpdateScrollBar: more rows than fit -> a bar exists (told the new count, or created over the bar rectangle with id 3);
// otherwise an existing bar is deleted. A failed allocation leaves the pointer null.
template<class Env> void UpdateScrollBar(ListBox& list, Env& env)
{
    if (list.m_rowCount > static_cast<uint32_t>(list.m_visibleRows)) {
        if (list.m_scroll) {
            env.SetItemCount(*list.m_scroll, list.m_rowCount);
            return;
        }
        void* memory = env.template NewRecord<ui::GuiScrollBarData>();
        if (!memory) {
            list.m_scroll = nullptr;
            return;
        }
        list.m_scroll = env.ConstructScroll(memory, list.m_barX0, list.m_barY0, list.m_barX1, list.m_barY1,
                static_cast<uint32_t>(list.m_scrollTop), list.m_rowCount, static_cast<uint32_t>(list.m_visibleRows), kScrollBarId);
    } else if (list.m_scroll) {
        env.DeleteWidget(*list.m_scroll);
        list.m_scroll = nullptr;
    }
}

// std::find over the selection (the loop unrolls four times; the result is the same). A reversed range finds nothing.
inline const int32_t* FindRow(const int32_t* first, const int32_t* last, int32_t row)
{
    if (last <= first) return last;
    for (const int32_t* it = first; it != last; ++it)
        if (*it == row) return it;
    return last;
}

// DrawOverlay: the bar's overlay first, then row by row of the window: a selected row gets a green band (the list's
// rectangle moved down by its slot), then its cells draw themselves - active buttons invalidate and draw, images switch to frame 1 on a
// selected row (0 otherwise) and draw, anything else only draws its overlay. The window and the cell vector are re-read every pass.
template<class Env> void DrawOverlay(ListBox& list, Env& env)
{
    if (list.m_scroll) env.DrawOverlay(*list.m_scroll);
    uint32_t row = static_cast<uint32_t>(list.m_scrollTop);
    if (row >= row + list.m_slotCount) return;
    do {
        bool selected = false;
        if (FindRow(list.m_selBegin, list.m_selEnd, static_cast<int32_t>(row)) != list.m_selEnd) {
            const int32_t rel = Mul(Sub(static_cast<int32_t>(row), list.m_scrollTop), list.m_rowHeight);
            int32_t rect[4] = {list.m_x0, rel, list.m_x1, 0};
            int32_t out[4];
            const int32_t top = Add(rel, env.Bounds(list, out)[1]);
            rect[1] = top;
            rect[3] = Add(list.m_rowHeight, top);
            env.FillSelection(rect);
            selected = true;
        }
        for (Cell** it = list.m_cellsBegin; it != list.m_cellsEnd; ++it) {
            Cell& cell = **it;
            if (cell.m_row != static_cast<int32_t>(row)) continue;
            Widget& widget = *cell.m_widget;
            if (widget.m_type == kCellButtonType) {
                if (!cell.m_active) continue;
                env.Invalidate(widget);
                env.Draw(widget);
            } else if (widget.m_type == kCellImageType) {
                env.SetFrame(widget, selected ? 1 : 0);
                env.Draw(widget);
            } else {
                env.DrawOverlay(widget);
            }
        }
        ++row;
    } while (row < static_cast<uint32_t>(list.m_scrollTop) + list.m_slotCount);
}

} // namespace obl::gui_lists

namespace obl::ui {
struct GuiImageList;
struct GuiList;
struct GuiListData;
struct GuiListItem;
struct GuiListBox;
struct GuiListBoxData;
} // namespace obl::ui

namespace obl::gui {
// The list box, the combo box list (GuiList) and the image list.
bool RegisterGuiLists();

// The combo box list's constructor, AddItem, ClearItems and OnItemSelected: the script parser and the dialogs call them directly.
ui::GuiList* GuiListConstruct(ui::GuiList* list, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* name, uint32_t color,
        int32_t font, int32_t lines, int32_t style, int32_t direction);
void GuiListAddItem(ui::GuiListData* list, const char* key, int32_t value);
void GuiListClearItems(ui::GuiListData* list);
void GuiListOnItemSelected(ui::GuiListData* list, ui::GuiListItem* item);
// The image list constructor: the script parser builds image lists directly.
ui::GuiImageList* GuiImageListConstruct(ui::GuiImageList* list, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* name,
        const char* highlight, int32_t spacingX, int32_t spacingY);

namespace listbox {

// Group 3: lifecycle and the row API (Construct, ClearRows, ClearSelection, NextSelectedRow, AddImage, AddText, AddTextA,
// SetRowButton, PollRowButton). The destructor has no facade: only the virtual DeletingDestruct reaches it.
ui::GuiListBox* Construct(ui::GuiListBox* list, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t barX0, int32_t barY0, int32_t barX1,
        int32_t barY1, const char* name, int32_t rowHeight, int32_t visibleRows);
void ClearRows(ui::GuiListBoxData* list);
void ClearSelection(ui::GuiListBoxData* list);
int32_t NextSelectedRow(ui::GuiListBoxData* list);
void AddImage(ui::GuiListBoxData* list, int32_t row, int32_t offset, const char* graphic);
void AddText(ui::GuiListBoxData* list, int32_t row, int32_t offset, int32_t width, uint32_t unused, uint32_t flags, int32_t font,
        uint32_t color, const Char16* text, uint32_t newRow);
void AddTextA(ui::GuiListBoxData* list, int32_t row, int32_t offset, int32_t width, uint32_t unused, uint32_t flags, int32_t font,
        uint32_t color, const char* text, uint32_t newRow);
void SetRowButton(ui::GuiListBoxData* list, int32_t row, int32_t offset, const char* graphic, int32_t value);
uint8_t PollRowButton(ui::GuiListBoxData* list, int32_t row, int32_t* value);

// Group 4: selection and scrolling (SelectRow, SetScrollTop, EnsureRowVisible, UpdateScrollBar).
void SelectRow(ui::GuiListBoxData* list, uint32_t row, int32_t replace);
void SetScrollTop(ui::GuiListBoxData* list, int32_t top);
void EnsureRowVisible(ui::GuiListBoxData* list, uint32_t row);
void UpdateScrollBar(ui::GuiListBoxData* list);

// Group 5: the list box's overrides of the common widget virtuals (Update, Draw, Invalidate, Translate, OnMouseLeft, OnMouseWheel,
// SetVisible, DrawOverlay): GuiListBox's virtual functions call these.
void Update(ui::GuiListBoxData* list, uint32_t dtBits);
void Draw(ui::GuiListBoxData* list);
void Invalidate(ui::GuiListBoxData* list);
void Translate(ui::GuiListBoxData* list, int32_t dx, int32_t dy);
uint8_t OnMouseLeft(ui::GuiListBoxData* list, int32_t x, int32_t y, int32_t state);
uint8_t OnMouseWheel(ui::GuiListBoxData* list, int32_t x, int32_t y, int32_t delta);
void SetVisible(ui::GuiListBoxData* list, uint32_t visible);
void DrawOverlay(ui::GuiListBoxData* list);

} // namespace listbox
} // namespace obl::gui

// The virtual functions of the GUI widgets.
