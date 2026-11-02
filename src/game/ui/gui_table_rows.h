#pragma once

#include "core/char16.h"
#include "game/ui/gui_tables.h"
#include "game/ui/gui_controls_types.h"

#include <cstdio>
#include <string>

namespace obl::gui_tables {

using Button = ui::GuiButtonData;
using NumberAdjust = ui::GuiNumberAdjustData;
using Image = ui::GuiVariableImageData;

// Indices into `k_GuiTableButtonResources`: pointers to the image and tooltip keys of the row buttons.
enum ButtonResource : int {
    ResGotoImage = 0,
    ResMoreInfoTip = 1,
    ResHoursDownTip = 2,
    ResHoursUpTip = 3,
    ResGotoHomeTip = 4,
    ResThoughtTip = 5,
    ResHoursUpImage = 6,
    ResHoursDownImage = 7,
};

inline constexpr int kButtonCount = 5;
inline constexpr size_t kRowKeySlack = 0x1f; // the key buffer is strlen + 1 + 0x1f bytes
inline constexpr size_t kObjectTextWords = 0x1f8; // text buffer of DrawObjectRowOverlay
inline constexpr size_t kIndexedTextWords = 0x100; // text buffer of DrawIndexedRowOverlay
inline constexpr int32_t kSelectionRed = 0x1a, kSelectionGreen = 0xdb, kSelectionBlue = 0x7b;

inline bool NameIs(const Column& column, const char* name)
{
    return std::strcmp(column.m_name, name) == 0;
}
inline Widget& ButtonAt(Column& column)
{
    return *static_cast<Widget*>(column.m_buttons.m_begin[column.m_cursor]);
}
inline Widget& AdjustAt(Column& column)
{
    return *static_cast<Widget*>(column.m_numberAdjusts.m_begin[column.m_cursor]);
}
inline NumberAdjust& NumberAdjustAt(Column& column)
{
    return *static_cast<NumberAdjust*>(column.m_numberAdjusts.m_begin[column.m_cursor]);
}

// The panel columns named GUEST / BUILDING keep their filter cell on the list selection of that panel; any other name leaves it alone.
template<class Env> void ApplyPanelFilter(Column& column, Env& env)
{
    if (NameIs(column, "GUEST"))
        env.SetCellFilter(*column.m_filterCell, env.GuestFilterSelection());
    else if (NameIs(column, "BUILDING"))
        env.SetCellFilter(*column.m_filterCell, env.BuildingFilterSelection());
}

// Drawing methods move the row cursor and the y offset, updating ones only the cursor.
inline void NextWidget(Column& column)
{
    column.m_cursor = Add(column.m_cursor, 1);
}
inline void NextRow(Column& column)
{
    column.m_rowY = Add(column.m_rowY, column.m_rowHeight);
    NextWidget(column);
}

template<class Env> void UpdateObjectRow(Column& column, float dt, void* object, Env& env)
{
    if (column.m_kind == ColumnButtons) {
        Widget& button = ButtonAt(column);
        env.UpdateWidget(button, dt);
        if (env.WasClicked(button)) {
            button.m_clicked = 0;
            if (NameIs(column, "GOTO"))
                env.OpenObject(object);
            else if (NameIs(column, "GOTO_HOME"))
                env.OpenObject(env.FindObject(env.StaffHomeId(object)));
            else if (NameIs(column, "STAFF_HOUR_DEC"))
                env.DecrementShiftHours(object);
            else if (NameIs(column, "STAFF_HOUR_INC"))
                env.IncrementShiftHours(object);
        }
    } else if (column.m_kind == ColumnFilter) {
        ApplyPanelFilter(column, env);
    } else if (column.m_kind == ColumnNumberAdjust) {
        if (object) NumberAdjustAt(column).m_provider = object;
        env.UpdateWidget(AdjustAt(column), dt);
    }
    NextWidget(column);
}

template<class Env> void UpdateIndexedRow(Column& column, float dt, void* provider, int32_t row, Env& env)
{
    if (column.m_kind == ColumnButtons) {
        Widget& button = ButtonAt(column);
        env.UpdateWidget(button, dt);
        if (env.WasClicked(button)) {
            button.m_clicked = 0;
            if (NameIs(column, "GOTO_THOUGHT")) env.OpenObject(env.FindObject(env.ThoughtObjectId(provider, row)));
        }
    } else if (column.m_kind == ColumnNumberAdjust) {
        if (provider) NumberAdjustAt(column).m_provider = provider;
        env.UpdateWidget(AdjustAt(column), dt);
    }
    NextWidget(column);
}

template<class Env> void DrawRow(Column& column, void* provider, bool selected, Env& env)
{
    if (column.m_kind == ColumnButtons) {
        env.DrawWidget(ButtonAt(column));
    } else if (column.m_kind == ColumnFilter) {
        ApplyPanelFilter(column, env);
        env.DrawCell(*column.m_filterCell, Add(column.m_rowY, column.m_y0), provider, column.m_font);
    } else if (column.m_kind == ColumnNumberAdjust) {
        if (selected) env.Invalidate(AdjustAt(column));
        env.DrawWidget(AdjustAt(column));
    }
    NextRow(column);
}

template<class Env> void DrawObjectRowOverlay(Column& column, void* object, bool selected, Env& env)
{
    const int32_t top = Add(column.m_rowY, column.m_y0);
    const int32_t width = Sub(column.m_x1, column.m_x0);
    void* renderer = env.TextRenderer(column.m_font);
    if (selected) env.FillRect(column.m_x0, top, column.m_x1, Sub(Add(column.m_rowHeight, top), 1));
    switch (column.m_kind) {
    case ColumnVariable: {
        Char16 text[kObjectTextWords];
        text[0] = 0;
        env.GetText(object, column.m_name, text);
        env.SetAlign(renderer, 0);
        env.SetOutlined(renderer, 0);
        env.SetColor(renderer, column.m_color);
        env.SetRect(renderer, column.m_x0, top, width, column.m_rowHeight);
        env.Truncate(renderer, text, width);
        env.DrawWrapped(renderer, column.m_xOffset, 0, text);
        break;
    }
    case ColumnImage:
        column.m_image->m_object = object;
        env.SetPosition(*column.m_image, Add(column.m_xOffset, column.m_x0), top);
        env.DrawWidgetOverlay(*column.m_image);
        break;
    case ColumnButtons:
        if (selected) {
            env.Invalidate(ButtonAt(column));
            env.DrawWidget(ButtonAt(column));
        }
        break;
    case ColumnFilter:
        ApplyPanelFilter(column, env);
        env.DrawCellOverlay(*column.m_filterCell, top, object, column.m_font);
        break;
    case ColumnNumberAdjust:
        if (selected) {
            NumberAdjustAt(column).m_provider = object;
            env.Invalidate(AdjustAt(column));
            env.DrawWidget(AdjustAt(column));
        }
        NumberAdjustAt(column).m_provider = object;
        env.DrawWidgetOverlay(AdjustAt(column));
        break;
    default: break;
    }
    NextRow(column);
}

template<class Env> void DrawIndexedRowOverlay(Column& column, void* provider, int32_t row, Env& env)
{
    if (column.m_kind == ColumnVariable) {
        const int32_t top = Add(column.m_rowY, column.m_y0);
        const int32_t width = Sub(column.m_x1, column.m_x0);
        void* renderer = env.TextRenderer(column.m_font);
        env.SetAlign(renderer, 0);
        env.SetOutlined(renderer, 0);
        env.SetColor(renderer, column.m_color);
        env.SetRect(renderer, column.m_x0, top, width, column.m_rowHeight);
        std::string key(std::strlen(column.m_name) + 1 + kRowKeySlack, '\0');
        std::snprintf(key.data(), key.size(), "%s_%d", column.m_name, static_cast<int>(row));
        Char16 text[kIndexedTextWords];
        text[0] = 0;
        env.GetText(provider, key.c_str(), text);
        if (column.m_flag == 0) env.Truncate(renderer, text, width);
        env.DrawWrapped(renderer, 0, 0, text);
    } else if (column.m_kind == ColumnNumberAdjust) {
        env.DrawWidgetOverlay(AdjustAt(column));
    }
    NextRow(column);
}

template<class Env> void PushWidget(Vector& vector, void* widget, Env& env)
{
    if (vector.m_end != vector.m_capacity) {
        *vector.m_end++ = widget;
        return;
    }
    const uint32_t length = static_cast<uint32_t>(vector.m_end - vector.m_begin);
    const uint32_t capacity = (length < 1 ? 1 : length) + length;

    const uint32_t bytes = capacity * sizeof(void*);
    void** storage = capacity * 4 <= kSmallBlockBytes ? static_cast<void**>(env.AllocSmall(bytes)) : static_cast<void**>(env.Alloc(bytes));
    void** at = storage;
    if (vector.m_end != vector.m_begin) {
        std::memmove(storage, vector.m_begin, length * sizeof(void*));
        at = storage + length;
    }
    *at = widget;
    FreeVectorStorage(vector, env);
    vector.m_begin = storage;
    vector.m_end = at + 1;
    vector.m_capacity = storage + capacity;
}

struct ButtonSpec {
    const char* name;
    ButtonResource image, tip;
    uint8_t repeat;
};
inline constexpr ButtonSpec kButtonSpecs[kButtonCount] = {
        {"GOTO", ResGotoImage, ResMoreInfoTip, 0},
        {"GOTO_THOUGHT", ResGotoImage, ResThoughtTip, 0},
        {"GOTO_HOME", ResGotoImage, ResGotoHomeTip, 0},
        {"STAFF_HOUR_DEC", ResHoursDownImage, ResHoursDownTip, 1},
        {"STAFF_HOUR_INC", ResHoursUpImage, ResHoursUpTip, 1},
};

template<class Env> void ResizeRowWidgets(Column& column, int32_t count, Env& env)
{
    if (count == column.m_rowWidgets) return;
    column.m_rowWidgets = count;
    if (column.m_kind == ColumnButtons) {
        Vector& vector = column.m_buttons;
        const uint32_t have = static_cast<uint32_t>(vector.m_end - vector.m_begin);
        if (have < static_cast<uint32_t>(count)) {
            const ButtonSpec* spec = nullptr;
            for (const ButtonSpec& candidate : kButtonSpecs) {
                if (NameIs(column, candidate.name)) {
                    spec = &candidate;
                    break;
                }
            }
            for (uint32_t made = 0; made < static_cast<uint32_t>(column.m_rowWidgets) - have; ++made) {
                if (!spec) continue;
                const int32_t x0 = Add(column.m_xOffset, column.m_x0);
                const int32_t y0 = Add(static_cast<int32_t>(static_cast<uint32_t>(vector.m_end - vector.m_begin) *
                                               static_cast<uint32_t>(column.m_rowHeight)),
                        column.m_y0);
                Button* button = env.ConstructButton(env.template NewRecord<Button>(), x0, y0, column.m_x1, Add(column.m_rowHeight, y0),
                        env.ButtonResource(spec->image));
                button->m_repeat = spec->repeat;
                env.SetTooltip(*button, env.ButtonResource(spec->tip));
                env.SetVisible(*button, true);
                PushWidget(vector, button, env);
            }
        }
        for (uint32_t surplus = static_cast<uint32_t>(vector.m_end - vector.m_begin); surplus > static_cast<uint32_t>(column.m_rowWidgets);
                --surplus) {
            Widget* widget = static_cast<Widget*>(*--vector.m_end);
            if (widget) env.DeleteWidget(*widget);
        }
    } else if (column.m_kind == ColumnNumberAdjust) {
        Vector& vector = column.m_numberAdjusts;
        const uint32_t have = static_cast<uint32_t>(vector.m_end - vector.m_begin);
        if (have < static_cast<uint32_t>(count)) {
            for (uint32_t made = 0; made < static_cast<uint32_t>(column.m_rowWidgets) - have; ++made) {
                const int32_t y0 = Add(static_cast<int32_t>(static_cast<uint32_t>(vector.m_end - vector.m_begin) *
                                               static_cast<uint32_t>(column.m_rowHeight)),
                        column.m_y0);
                std::string key(std::strlen(column.m_name) + 1 + kRowKeySlack, '\0');
                std::snprintf(key.data(), key.size(), "%s_%u", column.m_name, made);
                NumberAdjust* adjust = env.ConstructNumberAdjust(env.template NewRecord<NumberAdjust>(), column.m_x0, y0, column.m_x1,
                        Add(column.m_rowHeight, y0), column.m_flag == 0 ? 1 : 0, column.m_font, key.c_str());
                env.SetVisible(*adjust, true);
                PushWidget(vector, adjust, env);
            }
        }
        for (uint32_t surplus = static_cast<uint32_t>(vector.m_end - vector.m_begin); surplus > static_cast<uint32_t>(column.m_rowWidgets);
                --surplus) {
            Widget* widget = static_cast<Widget*>(*--vector.m_end);
            if (widget) env.DeleteWidget(*widget);
        }
    }
}

} // namespace obl::gui_tables
