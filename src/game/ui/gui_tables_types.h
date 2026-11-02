#pragma once

#include "game/ui/gui_controls_types.h"

#include <cstdint>

namespace obl::ui {

// Helper of a `FILTER_` column (no vtable). `Construct` leaves the last two fields untouched.
struct GuiTableFilterCell {
    GuiBarData* m_bar; // owned value bar (mode 0), deleted through its virtual destructor
    int32_t m_x0, m_y0, m_x1, m_y1; // current rectangle
    int32_t m_baseX0, m_baseY0, m_baseX1, m_baseY1; // rectangle SetOrigin offsets
    int32_t m_mode; // 0 bar, 1 text, 2 nothing (set by SetFilter)
    const char* m_variableKey; // variable of the text mode
};

// One record of `k_GuiTableFilterCellDefinitions` (28 records).
struct GuiTableFilterDefinition {
    uint32_t m_filter;
    int32_t m_mode;
    const char* m_key;
    uint32_t m_minBits, m_maxBits; // float bits, passed on untouched to GuiBarConstruct
    int32_t m_barKind;
};

// One record of `k_GuiTableFilterHeadingKeys` (27 records).
struct GuiTableHeadingKey {
    const char* m_key;
    uint32_t m_filter;
};

struct GuiPointerVector {
    void** m_begin = nullptr;
    void** m_end = nullptr;
    void** m_capacity = nullptr;
};

// A table column (type 0xf, built by ConstructColumn, a widget with virtual functions). `m_kind` comes from the name prefix
// (0 VAR_, 1 VARIMG_, 2 anything else, 3 FILTER_, 4 NUMADJ_); kinds 0 and 2 both drop the first four characters of the script name.
struct GuiTableColumnData : GuiWidget {
    using GuiWidget::GuiWidget;
    char* m_name = nullptr; // script name without the prefix, on the game heap
    int32_t m_rowHeight = 0; // height of one table row, set by the table
    uint32_t m_color = 0; // constructor argument 7
    int32_t m_font = 0; // constructor argument 6
    int32_t m_rowY = 0; // y offset of the row being drawn: the table clears it per pass, each row call adds m_rowHeight
    int32_t m_kind = 0;
    GuiVariableImageData* m_image = nullptr; // kind 1: owned picture, else null
    GuiPointerVector m_buttons; // kind 2: owned row buttons
    GuiPointerVector m_numberAdjusts; // kind 4: owned number adjusters
    int32_t m_rowWidgets = 0; // number of row widgets ResizeRowWidgets built
    int32_t m_cursor = 0; // row the table is updating
    GuiTableFilterCell* m_filterCell = nullptr; // kind 3: owned helper, else null
    int32_t m_flag = 0; // constructor argument 8 (kind 4 selects the NumberAdjust variant)
    int32_t m_xOffset = 0; // constructor argument 9 (added to x0 of the picture and the filter cell)
};

// gui_tables.cpp
struct GuiTableColumn final : GuiLeaf<GuiTableColumnData> {
    using GuiLeaf::GuiLeaf;
    GuiTableColumn() = default;
    GuiTableColumn(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, int flag, int xOffset);
    ~GuiTableColumn() override;
    void Invalidate() override;
    void SetVisible(uint32_t visible) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    bool MouseLeft(int x, int y, int state) override;
    const char* ClassName() const override { return "GuiTableColumn"; }
};

static_assert(sizeof(GuiTableColumn) == sizeof(GuiTableColumnData));

struct GuiPointerListNode {
    GuiPointerListNode* m_next;
    GuiPointerListNode* m_prev;
    void* m_value;
};

// A table. `m_dataKind` and `m_rowSource` come from the script name (GUESTS, BUILDINGS, ...); a name outside the nine known ones
// leaves both zero.
struct GuiTableData : GuiWidget {
    using GuiWidget::GuiWidget;
    int32_t m_rowHeight = 0; // constructor argument, also handed to every column (AddChild)
    uint32_t m_firstRow = 0; // first visible row (the scroll bar owns the truth when there is one)
    int32_t m_dataKind = 0; // 0 guests .. 6 staff, 7 DEBUG_INFO, 8 SENT_INFO (kinds 7/8 have no row source)
    int32_t m_rowSource = 0; // 0: one shared provider + row index, 1: one object per row
    uint32_t m_count = 0; // rows the data has
    GuiScrollBarData* m_scrollBar = nullptr; // created while count > limit
    uint32_t m_limit = 0; // rows that fit on the panel (constructor argument)
    GuiPointerVector m_rows; // row objects (or the single provider)
    GuiPointerListNode* m_columns = nullptr; // sentinel of the column list
    int32_t m_barX0 = 0, m_barY0 = 0, m_barX1 = 0, m_barY1 = 0; // where the scroll bar goes
    int32_t m_baseBarX0 = 0, m_baseBarY0 = 0, m_baseBarX1 = 0, m_baseBarY1 = 0; // the same before SetOrigin
    uint32_t m_visibleRows = 0; // min(count, limit)
    uint8_t m_sortParam = 0; // constructor sets 1 (sort direction of the row source)
    float m_refreshTimer = 0.0f; // seconds to the next rebuild of the row data (constructor 4.0)
    uint8_t m_autoSelect = 0; // constructor argument: a row is always selected
    int32_t m_selected = 0; // selected row, -1 none
};

// gui_tables.cpp
struct GuiTable final : GuiLeaf<GuiTableData> {
    using GuiLeaf::GuiLeaf;
    GuiTable() = default;
    GuiTable(int x0, int y0, int x1, int y1, int barX0, int barY0, int barX1, int barY1, const char* name, int rowHeight, uint32_t limit,
            uint8_t autoSelect);
    ~GuiTable() override;
    void Update(float dt) override;
    void Refresh() override;
    void Draw() override;
    void DrawOverlay() override;
    void AddChild(GuiWidget* child) override;
    void SetOrigin(int dx, int dy) override;
    void Translate(int dx, int dy) override;
    void Invalidate() override;
    void SetVisible(uint32_t visible) override;
    bool MouseWheel(int x, int y, int delta) override;
    bool MouseLeft(int x, int y, int state) override;
    const char* ClassName() const override { return "GuiTable"; }
};

static_assert(sizeof(GuiTable) == sizeof(GuiTableData));

} // namespace obl::ui
