#pragma once

#include "game/ui/gui_tables_types.h"

#include <cstdint>
#include <cstring>
#include <span>

namespace obl::gui_tables {

using Bar = ui::GuiBarData;
using Cell = ui::GuiTableFilterCell;
using Column = ui::GuiTableColumnData;
using Vector = ui::GuiPointerVector;
using Widget = ui::GuiWidget;

inline constexpr int kFilterDefinitionCount = 28; // records of k_GuiTableFilterCellDefinitions
inline constexpr int32_t kColumnType = 0xf;
inline constexpr uint32_t kSmallBlockBytes = 0x80;
inline constexpr int32_t kFilterCellHeight = 13; // text height of a FILTER_ column cell
inline constexpr int32_t kFilterTextHeight = 15;

enum ColumnKind : int32_t { ColumnVariable = 0, ColumnImage = 1, ColumnButtons = 2, ColumnFilter = 3, ColumnNumberAdjust = 4 };

inline int32_t Add(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
inline int32_t Sub(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

// ---- GuiTableFilterCell ---------------------------------------------------------------------------------------------------------

// Construct: only the rectangles, the bar slot and the global filter are written (mode and key stay as the heap left them).
template<class Env> Cell* ConstructFilterCell(Cell* cell, int32_t x0, int32_t y0, int32_t x1, int32_t y1, Env& env)
{
    cell->m_x0 = cell->m_baseX0 = x0;
    cell->m_y0 = cell->m_baseY0 = y0;
    cell->m_x1 = cell->m_baseX1 = x1;
    cell->m_y1 = cell->m_baseY1 = y1;
    cell->m_bar = nullptr;
    env.ActiveFilter() = 0;
    return cell;
}

// Destruct.
template<class Env> void DestructFilterCell(Cell& cell, Env& env)
{
    if (cell.m_bar) env.DeleteWidget(*cell.m_bar);
}

// Delete (null is ignored).
template<class Env> void DeleteFilterCell(Cell* cell, Env& env)
{
    if (!cell) return;
    if (cell->m_bar) env.DeleteWidget(*cell->m_bar);
    env.Free(cell);
}

// SetFilter: the active filter is one global shared by all cells; an unchanged value rebuilds nothing.
template<class Env> void SetFilter(Cell& cell, uint32_t filter, Env& env)
{
    uint32_t& active = env.ActiveFilter();
    if (active == filter) return;
    active = filter;
    if (cell.m_bar) {
        env.DeleteWidget(*cell.m_bar);
        cell.m_bar = nullptr;
    }
    const ui::GuiTableFilterDefinition* found = nullptr;
    const ui::GuiTableFilterDefinition* table = env.Definitions();
    for (int i = 0; i < kFilterDefinitionCount; ++i) {
        if (table[i].m_filter == env.ActiveFilter()) {
            found = &table[i];
            break;
        }
    }
    if (!found) {
        cell.m_mode = 2;
        return;
    }
    cell.m_mode = found->m_mode;
    if (found->m_mode == 0) {
        void* memory = env.template NewRecord<Bar>();
        cell.m_bar = memory ? env.ConstructBar(memory, cell.m_x0, cell.m_y0, cell.m_x1, cell.m_y1, found->m_minBits, found->m_maxBits,
                                      found->m_key, found->m_barKind)
                            : nullptr;
    } else if (found->m_mode == 1) {
        cell.m_variableKey = found->m_key;
    }
}

// Shared first half of Draw and DrawOverlay for the bar mode: the bar keeps its x, takes the row's y and the provider, then is redrawn.
template<class Env> void PlaceBar(Cell& cell, int32_t y, void* provider, Env& env)
{
    Bar& bar = *cell.m_bar;
    env.SetPosition(bar, bar.m_x0, y);
    bar.m_provider = provider;
    env.Invalidate(bar);
}

// Draw (the font is not read).
template<class Env> void DrawFilterCell(Cell& cell, int32_t y, void* provider, Env& env)
{
    if (cell.m_mode != 0 || !cell.m_bar) return;
    PlaceBar(cell, y, provider, env);
    env.DrawWidget(*cell.m_bar);
}

// DrawOverlay: the bar mode draws the bar overlay, the text mode prints the variable in black over the cell.
template<class Env> void DrawFilterCellOverlay(Cell& cell, int32_t y, void* provider, int32_t font, Env& env)
{
    if (cell.m_mode == 0) {
        if (!cell.m_bar) return;
        PlaceBar(cell, y, provider, env);
        env.DrawWidgetOverlay(*cell.m_bar);
    } else if (cell.m_mode == 1) {
        env.DrawVariableText(font, provider, cell.m_variableKey, cell.m_x0, y, Sub(cell.m_x1, cell.m_x0), kFilterTextHeight);
    }
}

// Translate.
template<class Env> void TranslateFilterCell(Cell& cell, int32_t dx, int32_t dy, Env& env)
{
    cell.m_x0 = Add(cell.m_x0, dx);
    cell.m_x1 = Add(cell.m_x1, dx);
    cell.m_y0 = Add(cell.m_y0, dy);
    cell.m_y1 = Add(cell.m_y1, dy);
    if (cell.m_bar) env.Translate(*cell.m_bar, dx, dy);
}

// SetOrigin (the bar is not moved here).
inline void SetFilterCellOrigin(Cell& cell, int32_t dx, int32_t dy)
{
    cell.m_x0 = Add(cell.m_baseX0, dx);
    cell.m_x1 = Add(cell.m_baseX1, dx);
    cell.m_y0 = Add(cell.m_baseY0, dy);
    cell.m_y1 = Add(cell.m_baseY1, dy);
}

// GetHeadingKey: heading text key of the active filter, null when it has none.
template<class Env> const char* GetHeadingKey(Env& env)
{
    for (const ui::GuiTableHeadingKey& heading : env.HeadingKeys()) {
        if (heading.m_filter == env.ActiveFilter()) return heading.m_key;
    }
    return nullptr;
}

// ---- GuiTableColumn -------------------------------------------------------------------------------------------------------------

// Prefix of the script name decides the kind; kinds 0 and 2 both skip four characters, the others seven.
inline ColumnKind KindOfName(const char* key, int& prefix)
{
    if (std::strncmp(key, "VAR_", 4) == 0) {
        prefix = 4;
        return ColumnVariable;
    }
    if (std::strncmp(key, "VARIMG_", 7) == 0) {
        prefix = 7;
        return ColumnImage;
    }
    if (std::strncmp(key, "FILTER_", 7) == 0) {
        prefix = 7;
        return ColumnFilter;
    }
    if (std::strncmp(key, "NUMADJ_", 7) == 0) {
        prefix = 7;
        return ColumnNumberAdjust;
    }
    prefix = 4;
    return ColumnButtons;
}

template<class Env>
Column* ConstructColumn(Column* column, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* key, int32_t font, uint32_t color,
        int32_t flag, int32_t xOffset, Env& env)
{
    column->m_flag = flag;
    column->m_xOffset = xOffset;
    column->m_type = kColumnType;
    const int32_t left = Add(x0, xOffset);
    int prefix = 0;
    column->m_kind = KindOfName(key, prefix);
    const size_t length = std::strlen(key + prefix) + 1;
    column->m_name = static_cast<char*>(env.Alloc(std::strlen(key) + 1 - prefix));
    std::memcpy(column->m_name, key + prefix, length);
    if (column->m_kind == ColumnImage) {
        void* memory = env.template NewRecord<ui::GuiVariableImageData>();
        column->m_image = memory ? env.ConstructImage(memory, left, y0, x1, y1, column->m_name) : nullptr;
    } else if (column->m_kind == ColumnFilter) {
        Cell* memory = env.template NewRecord<Cell>();
        column->m_filterCell = memory ? ConstructFilterCell(memory, left, y0, x1, Add(y0, kFilterCellHeight), env) : nullptr;
    }
    column->m_font = font;
    column->m_color = color;
    return column;
}

template<class Env> void FreeVectorStorage(Vector& vector, Env& env)
{
    if (!vector.m_begin) return;

    const uint32_t slots = static_cast<uint32_t>(vector.m_capacity - vector.m_begin);
    if (slots * 4 > kSmallBlockBytes)
        env.Free(vector.m_begin);
    else
        env.FreeSmall(vector.m_begin, static_cast<uint32_t>(slots * sizeof(void*)));
}

// ClearRowWidgets: the buttons are deleted, the number adjusters hidden and deleted; the vectors keep their storage.
template<class Env> void ClearRowWidgets(Column& column, Env& env)
{
    for (void** it = column.m_buttons.m_begin; it != column.m_buttons.m_end; ++it) {
        if (*it) env.DeleteWidget(*static_cast<Widget*>(*it));
    }
    column.m_buttons.m_end = column.m_buttons.m_begin;
    for (void** it = column.m_numberAdjusts.m_begin; it != column.m_numberAdjusts.m_end; ++it) {
        env.SetVisible(*static_cast<Widget*>(*it), false);
        if (*it) env.DeleteWidget(*static_cast<Widget*>(*it));
    }
    column.m_numberAdjusts.m_end = column.m_numberAdjusts.m_begin;
    column.m_rowWidgets = 0;
}

// Destruct.
template<class Env> void DestructColumn(Column& column, Env& env)
{
    env.BaseSetVisible(column, false);
    ClearRowWidgets(column, env);
    if (column.m_image) env.DeleteWidget(*column.m_image);
    if (column.m_filterCell) {
        DestructFilterCell(*column.m_filterCell, env);
        env.Free(column.m_filterCell);
    }
    env.Free(column.m_name);
    FreeVectorStorage(column.m_numberAdjusts, env);
    FreeVectorStorage(column.m_buttons, env);
}

// SetVisible: hiding drops the row widgets; showing a FILTER_ column named GUEST / BUILDING selects the filter of that panel.
template<class Env> void SetColumnVisible(Column& column, bool visible, Env& env)
{
    env.BaseSetVisible(column, visible);
    if (!visible) {
        ClearRowWidgets(column, env);
        return;
    }
    if (column.m_kind != ColumnFilter) return;
    if (std::strcmp(column.m_name, "GUEST") == 0)
        SetFilter(*column.m_filterCell, env.GuestFilterSelection(), env);
    else if (std::strcmp(column.m_name, "BUILDING") == 0)
        SetFilter(*column.m_filterCell, env.BuildingFilterSelection(), env);
}

// SetOrigin and Translate differ only in the base call, the vtable slot and the filter cell function.
template<class Env> void SetColumnOrigin(Column& column, int32_t dx, int32_t dy, Env& env)
{
    env.BaseSetOrigin(column, dx, dy);
    switch (column.m_kind) {
    case ColumnButtons:
        for (void** it = column.m_buttons.m_begin; it != column.m_buttons.m_end; ++it) env.SetOrigin(*static_cast<Widget*>(*it), dx, dy);
        break;
    case ColumnFilter: SetFilterCellOrigin(*column.m_filterCell, dx, dy); break;
    case ColumnNumberAdjust:
        for (void** it = column.m_numberAdjusts.m_begin; it != column.m_numberAdjusts.m_end; ++it)
            env.SetOrigin(*static_cast<Widget*>(*it), dx, dy);
        break;
    default: break;
    }
}

template<class Env> void TranslateColumn(Column& column, int32_t dx, int32_t dy, Env& env)
{
    env.BaseTranslate(column, dx, dy);
    switch (column.m_kind) {
    case ColumnButtons:
        for (void** it = column.m_buttons.m_begin; it != column.m_buttons.m_end; ++it) env.Translate(*static_cast<Widget*>(*it), dx, dy);
        break;
    case ColumnFilter: TranslateFilterCell(*column.m_filterCell, dx, dy, env); break;
    case ColumnNumberAdjust:
        for (void** it = column.m_numberAdjusts.m_begin; it != column.m_numberAdjusts.m_end; ++it)
            env.Translate(*static_cast<Widget*>(*it), dx, dy);
        break;
    default: break;
    }
}

// Invalidate: the base, then every button and number adjuster.
template<class Env> void InvalidateColumn(Column& column, Env& env)
{
    env.BaseInvalidate(column);
    for (void** it = column.m_buttons.m_begin; it != column.m_buttons.m_end; ++it) env.Invalidate(*static_cast<Widget*>(*it));
    for (void** it = column.m_numberAdjusts.m_begin; it != column.m_numberAdjusts.m_end; ++it) env.Invalidate(*static_cast<Widget*>(*it));
}

// OnMouseLeft: tooltip tracking of the column, the click is offered to every row widget; the column never consumes it.
template<class Env> bool ColumnOnMouseLeft(Column& column, int32_t x, int32_t y, int32_t state, Env& env)
{
    env.TrackTooltip(column, x, y, state);
    for (void** it = column.m_buttons.m_begin; it != column.m_buttons.m_end; ++it) env.OnMouseLeft(*static_cast<Widget*>(*it), x, y, state);
    for (void** it = column.m_numberAdjusts.m_begin; it != column.m_numberAdjusts.m_end; ++it)
        env.OnMouseLeft(*static_cast<Widget*>(*it), x, y, state);
    return false;
}

} // namespace obl::gui_tables

namespace obl::ui {
struct GuiTableData;
struct GuiTable;
struct GuiTableColumn;
struct GuiTableColumnData;
struct GuiTableFilterCell;
} // namespace obl::ui

namespace obl::gui {
// The filter cell, column (rows included) and table frame implementations.
bool RegisterGuiTables();

namespace table {

void UpdateGuests(ui::GuiTableData* table, float dt);
void UpdateBuildings(ui::GuiTableData* table, float dt);
void UpdatePowerGenerators(ui::GuiTableData* table, float dt);
void UpdateBuildingsClosedPower(ui::GuiTableData* table, float dt);
void UpdateBuildingCommodities(ui::GuiTableData* table, float dt);
void UpdateStaff(ui::GuiTableData* table, float dt);

ui::GuiTable* Construct(ui::GuiTable* table, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t barX0, int32_t barY0, int32_t barX1,
        int32_t barY1, const char* name, int32_t rowHeight, uint32_t limit, uint8_t autoSelect);
void SyncScrollAndSelection(ui::GuiTableData* table);
void SetVisible(ui::GuiTableData* table, bool visible);
uint8_t OnMouseWheel(ui::GuiTableData* table, int32_t x, int32_t y, int32_t delta);
uint8_t OnMouseLeft(ui::GuiTableData* table, int32_t x, int32_t y, int32_t state);
void Refresh(ui::GuiTableData* table);
void Update(ui::GuiTableData* table, float dt);
void Invalidate(ui::GuiTableData* table);
void Draw(ui::GuiTableData* table);
void DrawOverlay(ui::GuiTableData* table);
void AddChild(ui::GuiTableData* table, ui::GuiTableColumnData* child);
void Translate(ui::GuiTableData* table, int32_t dx, int32_t dy);
void SetOrigin(ui::GuiTableData* table, int32_t dx, int32_t dy);
} // namespace table

// The filter_cell:: facades answer the GuiTableFilterCell methods that other code calls, with the cell first. The counter
// is the one of the table family (ui.gui-tables), counted inside the facades.
namespace filter_cell {
// The cell's selection and drawing (SetFilter, Draw, DrawOverlay). The draw takes no font.
void SetFilter(ui::GuiTableFilterCell* cell, uint32_t filter);
void Draw(ui::GuiTableFilterCell* cell, int32_t y, void* provider);
void DrawOverlay(ui::GuiTableFilterCell* cell, int32_t y, void* provider, int32_t font);
} // namespace filter_cell

// The table_column:: facades answer the GuiTableColumn row methods with the column first. The counter
// is the one of the table family (ui.gui-tables), counted inside the facades.
namespace table_column {
// The common widget slots (GuiTableColumn's virtual functions forward to them).
void SetVisible(ui::GuiTableColumnData* column, uint32_t visible);
void SetOrigin(ui::GuiTableColumnData* column, int32_t dx, int32_t dy);
void Translate(ui::GuiTableColumnData* column, int32_t dx, int32_t dy);
void Invalidate(ui::GuiTableColumnData* column);
uint8_t OnMouseLeft(ui::GuiTableColumnData* column, int32_t x, int32_t y, int32_t state);

// The per-row methods (src/game/ui/gui_table_rows.h). The indexed overlay takes no selection flag.
void UpdateObjectRow(ui::GuiTableColumnData* column, float dt, void* object);
void UpdateIndexedRow(ui::GuiTableColumnData* column, float dt, void* provider, int32_t row);
void DrawRow(ui::GuiTableColumnData* column, void* provider, bool selected);
void DrawObjectRowOverlay(ui::GuiTableColumnData* column, void* object, bool selected);
void DrawIndexedRowOverlay(ui::GuiTableColumnData* column, void* provider, int32_t row);
void ResizeRowWidgets(ui::GuiTableColumnData* column, int32_t count);
} // namespace table_column
} // namespace obl::gui

// The virtual functions of the GUI tables.
