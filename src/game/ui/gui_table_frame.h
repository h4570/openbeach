#pragma once

#include "core/pointer_width.h"
#include "game/objects/object_building.h"
#include "game/ui/gui_table_rows.h"
#include "game/objects/object_base.h"

#include <cstdint>
#include <cstring>

namespace obl::gui_tables {

using Table = ui::GuiTableData;
using ListNode = ui::GuiPointerListNode;

inline constexpr int32_t kTableType = 7;
inline constexpr uint32_t kListNodeBytes = sizeof(ListNode); // next, prev, value are native pointers
inline constexpr uint32_t kScrollBarId = 3;
inline constexpr float kRefreshPeriod = 4.0f; // seconds between rebuilds of the row data (and the `Refresh` argument is twice it)
inline constexpr int32_t kSelectedContext = 0xc; // GuiManager::SetContext index that receives the id of the clicked row

enum TableKind : int32_t {
    Guests = 0,
    Buildings = 1,
    Commodities = 2,
    Thoughts = 3,
    PowerGenerators = 4,
    ClosedPower = 5,
    Staff = 6,
    DebugInfo = 7,
    SentInfo = 8
};

struct TableName {
    const char* name;
    TableKind kind;
    int32_t rowSource;
};
inline constexpr TableName kTableNames[] = {
        {"GUESTS", Guests, 1},
        {"BUILDINGS", Buildings, 1},
        {"COMMODITIES", Commodities, 0},
        {"DEBUG_INFO", DebugInfo, 0},
        {"SENT_INFO", SentInfo, 0},
        {"BUILDING_THOUGHTS", Thoughts, 0},
        {"POWER_GENERATORS", PowerGenerators, 1},
        {"BUILDINGS_CLOSED_POWER", ClosedPower, 1},
        {"STAFF_LIST", Staff, 1},
};

inline Column& ColumnOf(const ListNode& node)
{
    return *static_cast<Column*>(node.m_value);
}

// The rectangle comes from GuiWidget's constructor, the tooltip goes with GuiLeaf's destructor.
template<class Env>
void ConstructTable(Table* table, int32_t barX0, int32_t barY0, int32_t barX1, int32_t barY1, const char* name, int32_t rowHeight,
        uint32_t limit, uint8_t autoSelect, Env& env)
{
    auto* sentinel = static_cast<ListNode*>(env.AllocSmall(kListNodeBytes));
    sentinel->m_next = sentinel;
    sentinel->m_prev = sentinel;
    table->m_columns = sentinel;
    table->m_barX0 = table->m_baseBarX0 = barX0;
    table->m_barY0 = table->m_baseBarY0 = barY0;
    table->m_barX1 = table->m_baseBarX1 = barX1;
    table->m_barY1 = table->m_baseBarY1 = barY1;
    table->m_sortParam = 1;
    table->m_refreshTimer = kRefreshPeriod;
    table->m_autoSelect = autoSelect;
    table->m_selected = -1;
    for (const TableName& known : kTableNames) {
        if (std::strcmp(name, known.name) == 0) {
            table->m_dataKind = known.kind;
            table->m_rowSource = known.rowSource;
            break;
        }
    }
    table->m_rowHeight = rowHeight;
    table->m_limit = limit;
    table->m_type = kTableType;
}

// The nodes of the column list go back to the pool one by one; the list keeps only its sentinel.
template<class Env> void ReleaseListNodes(ListNode* sentinel, Env& env)
{
    for (ListNode* node = sentinel->m_next; node != sentinel;) {
        ListNode* next = node->m_next;
        if (node) env.FreeSmall(node, kListNodeBytes);
        node = next;
    }
    sentinel->m_next = sentinel;
    sentinel->m_prev = sentinel;
}

// Destruct. The columns themselves belong to the dialog and are not deleted here; the scroll bar is.
template<class Env> void DestructTable(Table& table, Env& env)
{
    ListNode* sentinel = table.m_columns;
    ReleaseListNodes(sentinel, env);
    if (table.m_scrollBar) env.DeleteWidget(*table.m_scrollBar);
    ReleaseListNodes(sentinel, env);
    env.FreeSmall(sentinel, kListNodeBytes);
    FreeVectorStorage(table.m_rows, env);
}

// SetVisible: showing refreshes the data and redraws; hiding forgets the rows.
template<class Env> void SetTableVisible(Table& table, bool visible, Env& env)
{
    env.BaseSetVisible(table, visible);
    if (table.m_scrollBar) env.SetVisible(*table.m_scrollBar, visible);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) env.SetVisible(ColumnOf(*node), visible);
    if (visible) {
        env.RefreshTable(table);
        env.Invalidate(table);
        return;
    }
    table.m_visibleRows = 0;
    table.m_count = 0;
    table.m_rows.m_end = table.m_rows.m_begin;
}

// OnMouseWheel: only a table with a scroll bar reacts, and only while the cursor is over the table or the bar.
template<class Env> uint8_t TableOnMouseWheel(Table& table, int32_t x, int32_t y, int32_t delta, Env& env)
{
    if (!table.m_scrollBar) return 0;
    if (!env.HitTest(table, x, y) && !env.HitTest(*table.m_scrollBar, x, y)) return 0;
    return env.MouseWheel(*table.m_scrollBar, x, y, delta);
}

template<class Env> uint8_t TableOnMouseLeft(Table& table, int32_t x, int32_t y, int32_t state, Env& env)
{
    uint8_t result = 0;
    if (table.m_scrollBar) result = env.MouseLeft(*table.m_scrollBar, x, y, state);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) {
        if (result) return result;
        result = env.MouseLeft(ColumnOf(*node), x, y, state);
    }
    if (result || !table.m_autoSelect || table.m_count == 0 || state != 1 || !env.HitTest(table, x, y)) return result;
    const uint32_t row = static_cast<uint32_t>(Sub(y, table.m_y0)) / static_cast<uint32_t>(table.m_rowHeight) + table.m_firstRow;
    const int32_t last = Sub(static_cast<int32_t>(table.m_count), 1);
    table.m_selected = static_cast<int32_t>(row);
    if (table.m_selected > last) table.m_selected = last;
    int32_t id = -1;
    if (table.m_selected != -1) {
        void** rows = table.m_rows.m_begin;
        const void* object = table.m_rowSource == 0 ? rows[0] : rows[table.m_selected];
        id = static_cast<int32_t>(static_cast<const object::Base*>(object)->m_id);
    }
    env.SetContext(kSelectedContext, id);
    return result;
}

// What `Update` and `Refresh` share: the row data of the table's kind (a thoughts table keeps the current building as its single row).
template<class Env> void CollectRows(Table& table, float dt, Env& env)
{
    switch (static_cast<uint32_t>(table.m_dataKind)) {
    case Guests:
    case Buildings:
    case Commodities:
    case PowerGenerators:
    case ClosedPower:
    case Staff: env.CollectRows(table, table.m_dataKind, dt); break;
    case Thoughts: {
        table.m_rows.m_end = table.m_rows.m_begin;
        void* provider = env.VariableProvider();
        PushWidget(table.m_rows, provider, env);
        // The provider of a thought table is the selected building.
        table.m_count = static_cast<uint32_t>(static_cast<const object::Building*>(provider)->m_ThoughtList.size());
        break;
    }
    default: break;
    }
}

inline void ClampVisibleRows(Table& table)
{
    table.m_visibleRows = table.m_count;
    if (table.m_limit < table.m_count) table.m_visibleRows = table.m_limit;
}

// Refresh: rebuilds the rows now (a period of eight seconds is more than the timer needs), resizes the row widgets and redraws.
template<class Env> void RefreshTable(Table& table, Env& env)
{
    CollectRows(table, 2 * kRefreshPeriod, env);
    ClampVisibleRows(table);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next)
        env.ResizeColumn(ColumnOf(*node), static_cast<int32_t>(table.m_visibleRows));
    env.Invalidate(table);
}

// The row loops of Update / Draw / DrawOverlay run over [first, first + visible), re-reading both after each row.
template<class Body> void ForEachVisibleRow(Table& table, Body&& body)
{
    uint32_t row = table.m_firstRow;
    if (row < row + table.m_visibleRows) {
        do {
            body(row);
            ++row;
        } while (row < table.m_firstRow + table.m_visibleRows);
    }
}

inline void RestartColumn(Column& column)
{
    column.m_cursor = 0;
    column.m_rowY = 0;
}

inline void* RowOf(const Table& table, uint32_t row)
{
    return table.m_rowSource == 0 ? table.m_rows.m_begin[0] : table.m_rows.m_begin[row];
}

// Update: new data, the scroll bar (which owns the first row), then every column updates its widgets for the visible rows.
template<class Env> void UpdateTable(Table& table, float dt, Env& env)
{
    CollectRows(table, dt, env);
    ClampVisibleRows(table);
    if (table.m_scrollBar) {
        env.UpdateWidget(*table.m_scrollBar, dt);
        table.m_firstRow = table.m_scrollBar->m_top;
    }
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) {
        Column& column = ColumnOf(*node);
        env.ResizeColumn(column, static_cast<int32_t>(table.m_visibleRows));
        RestartColumn(column);
        ForEachVisibleRow(table, [&](uint32_t row) {
            if (table.m_rowSource == 0)
                env.UpdateColumnIndexed(column, dt, table.m_rows.m_begin[0], static_cast<int32_t>(row));
            else if (table.m_rowSource == 1)
                env.UpdateColumnObject(column, dt, table.m_rows.m_begin[row]);
        });
    }
}

// SyncScrollAndSelection: a bar exists exactly while there are more rows than fit; the selection is kept inside the data.
template<class Env> void SyncScrollAndSelection(Table& table, Env& env)
{
    if (table.m_limit < table.m_count) {
        if (!table.m_scrollBar) {
            table.m_firstRow = 0;
            void* memory = env.template NewRecord<ui::GuiScrollBarData>();
            table.m_scrollBar = memory ? env.ConstructScrollBar(memory, table.m_barX0, table.m_barY0, table.m_barX1, table.m_barY1,
                                                 table.m_firstRow, table.m_count, table.m_limit, kScrollBarId)
                                       : nullptr;
        } else {
            env.ScrollBarSetItemCount(*table.m_scrollBar, table.m_count);
            table.m_firstRow = table.m_scrollBar->m_top;
        }
    } else if (table.m_scrollBar) {
        env.DeleteWidget(*table.m_scrollBar);
        table.m_scrollBar = nullptr;
        table.m_firstRow = 0;
        ClampVisibleRows(table);
    }
    if (table.m_autoSelect && table.m_selected == -1) table.m_selected = 0;
    const int32_t last = Sub(static_cast<int32_t>(table.m_count), 1);
    if (table.m_selected > last) table.m_selected = last;
    if (table.m_count == 0) table.m_selected = -1;
}

// Invalidate.
template<class Env> void InvalidateTable(Table& table, Env& env)
{
    env.BaseInvalidate(table);
    if (table.m_scrollBar) env.Invalidate(*table.m_scrollBar);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) env.Invalidate(ColumnOf(*node));
}

inline bool RowSelected(const Table& table, uint32_t row)
{
    return table.m_autoSelect != 0 && static_cast<uint32_t>(table.m_selected) == row;
}

// Draw (an empty row vector draws nothing at all, the scroll bar included).
template<class Env> void DrawTable(Table& table, Env& env)
{
    if (table.m_rows.m_begin == table.m_rows.m_end) return;
    if (table.m_scrollBar) env.DrawWidget(*table.m_scrollBar);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) {
        Column& column = ColumnOf(*node);
        env.ResizeColumn(column, static_cast<int32_t>(table.m_visibleRows));
        RestartColumn(column);
        ForEachVisibleRow(table, [&](uint32_t row) { env.DrawColumnRow(column, RowOf(table, row), RowSelected(table, row)); });
    }
}

// DrawOverlay.
template<class Env> void DrawTableOverlay(Table& table, Env& env)
{
    if (table.m_rows.m_begin == table.m_rows.m_end) return;
    if (table.m_scrollBar) env.DrawWidgetOverlay(*table.m_scrollBar);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) {
        Column& column = ColumnOf(*node);
        env.ResizeColumn(column, static_cast<int32_t>(table.m_visibleRows));
        RestartColumn(column);
        ForEachVisibleRow(table, [&](uint32_t row) {
            const bool selected = RowSelected(table, row);
            if (table.m_rowSource == 0)
                env.DrawColumnIndexedOverlay(column, table.m_rows.m_begin[0], static_cast<int32_t>(row), selected);
            else
                env.DrawColumnObjectOverlay(column, table.m_rows.m_begin[row], selected);
        });
    }
}

// AddChild: the column is appended to the list and learns the row height.
template<class Env> void AddTableColumn(Table& table, Column* column, Env& env)
{
    auto* node = static_cast<ListNode*>(env.AllocSmall(kListNodeBytes));
    ListNode* sentinel = table.m_columns;
    node->m_next = sentinel;
    node->m_prev = sentinel->m_prev;
    node->m_value = column;
    sentinel->m_prev->m_next = node;
    sentinel->m_prev = node;
    column->m_rowHeight = table.m_rowHeight;
}

// Translate: the scroll bar rectangle moves with the table (the bar itself too, when there is one).
template<class Env> void TranslateTable(Table& table, int32_t dx, int32_t dy, Env& env)
{
    env.BaseTranslate(table, dx, dy);
    table.m_barX1 = Add(table.m_barX1, dx);
    table.m_barX0 = Add(table.m_barX0, dx);
    table.m_barY0 = Add(table.m_barY0, dy);
    table.m_barY1 = Add(table.m_barY1, dy);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) env.Translate(ColumnOf(*node), dx, dy);
    if (table.m_scrollBar) env.Translate(*table.m_scrollBar, dx, dy);
}

// SetOrigin: the columns follow; the rectangle of a bar that does not exist yet is recomputed from the base (an existing bar is not moved).
template<class Env> void SetTableOrigin(Table& table, int32_t dx, int32_t dy, Env& env)
{
    env.BaseSetOrigin(table, dx, dy);
    for (ListNode* node = table.m_columns->m_next; node != table.m_columns; node = node->m_next) env.SetOrigin(ColumnOf(*node), dx, dy);
    if (!table.m_scrollBar) {
        table.m_barX0 = Add(table.m_baseBarX0, dx);
        table.m_barX1 = Add(table.m_baseBarX1, dx);
        table.m_barY0 = Add(table.m_baseBarY0, dy);
        table.m_barY1 = Add(table.m_baseBarY1, dy);
    }
}

} // namespace obl::gui_tables
