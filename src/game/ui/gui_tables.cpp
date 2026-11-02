#include "core/char16.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/renderer_api.h"
#include "game/ui/text_renderer.h"
#include "game/ui/gui_widgets.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_widgets_image_bar.h"
#include "game/ui/gui_variable_lifecycle.h"
#include "game/ui/gui_controls.h"
#include "game/ui/gui_tables.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/gui_content_runtime.h"
#include "game/ui/gui_definitions.h"

#include "game/ui/gui_sort.h"
#include "game/game/game_accessors.h"
#include "game/game/command_loop.h"
#include "game/ui/gui_table_collect.h"
#include "game/ui/gui_table_frame.h"
#include "game/ui/gui_table_rows.h"
#include "game/ui/gui_tables_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/ui/gui_controls_types.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "core/log.h"
#include "game/objects/staff_wages.h"
#include "core/call_counter.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

// The table widgets: the filter cell, the column and the table frame. The logic is src/game/ui/gui_tables.h; this file
// adds the game side (virtual calls, base widget methods, heap). The row methods of the column (src/game/ui/gui_table_rows.h) and the table
// frame (src/common/gui_table_frame.h) are wired the same way.
namespace obl::gui {
namespace {

using Widget = ui::GuiWidget;
using Bar = ui::GuiBarData;
using Cell = ui::GuiTableFilterCell;
using Column = ui::GuiTableColumnData;
using Image = ui::GuiVariableImageData;
using Renderer = ui::TextRenderer;

using Table = ui::GuiTableData;

// Call counters (`call-report ui.gui-tables`).
test::CallCounter g_counter("ui.gui-tables");

// Thin wrappers over the game functions the table code calls; GameEnv below goes through them.
void __cdecl CallDelete(void* block)
{
    ::operator delete(block);
}
void CallSetAlign(Renderer* r, int32_t align)
{
    ui::font_text::SetAlign(r, align);
}
void CallSetOutlined(Renderer* r, uint32_t outlined)
{
    ui::font_text::SetOutlined(r, outlined);
}
void CallSetColor(Renderer* r, uint32_t rgb)
{
    ui::font_text::SetColor(r, rgb);
}
void CallSetRect(Renderer* r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    ui::font_text::SetRect(r, x, y, w, h);
}
void CallDrawWrapped(Renderer* r, int32_t x, int32_t y, const Char16* text)
{
    ui::font_text::DrawWrapped(r, x, y, text);
}

// The calls of the row methods that reach other game objects.
void* CallGetObject(void* manager, int32_t id)
{
    return object::object_manager::ObjectById(static_cast<object::Manager*>(manager), id);
}
void CallIncrement(void* staff)
{
    const auto& member = *static_cast<object::Staff*>(staff);
    int32_t start = member.m_StartTime, end = member.m_EndTime;
    staff::IncrementShiftHours(start, end);
    game::SubmitCommand(command::SetStaffShift{member.m_id, start, end});
}
void CallDecrement(void* staff)
{
    const auto& member = *static_cast<object::Staff*>(staff);
    int32_t start = member.m_StartTime, end = member.m_EndTime;
    staff::DecrementShiftHours(start, end);
    game::SubmitCommand(command::SetStaffShift{member.m_id, start, end});
}
test::CallCounter g_shiftUpCommand("GuiTableShiftUp");
test::CallCounter g_shiftDownCommand("GuiTableShiftDown");
template<bool up> void DriveCommandShift(int id)
{
    void* object = game::GetWorld() ? CallGetObject(game::GetObjectManager(), id) : nullptr;
    if (!object || static_cast<object::Base*>(object)->m_type < object::ObjectType::Lifeguard ||
            static_cast<object::Base*>(object)->m_type > object::ObjectType::SecurityGuard) {
        OBL_LOG("autotest: FAILED GuiTable shift driver needs a staff id");
        return;
    }
    if constexpr (up)
        CallIncrement(object);
    else
        CallDecrement(object);
}
int32_t CallThoughtId(void* building, int32_t row)
{
    return object::building_api::GetThoughtObjectId(static_cast<const object::Building*>(building), row);
}
void CallTruncate(Renderer* r, Char16* text, int32_t width)
{
    ui::font_text::TruncateWithEllipsis(r, text, width);
}

void CallFillRect(void* renderer, int32_t* rect, uint32_t color)
{
    obl::ui::renderer::FillRect(renderer, rect, color);
}

void* __cdecl CallNew(size_t bytes)
{
    return ::operator new(bytes);
}
void* CallBarConstruct(
        void* bar, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t top, uint32_t count, uint32_t visible, uint32_t id)
{
    return scrollbar_facade::Construct(static_cast<ui::GuiScrollBar*>(bar), x0, y0, x1, y1, top, count, visible, id);
}
void CallBarSetItemCount(void* bar, uint32_t count)
{
    scrollbar_facade::SetItemCount(static_cast<ui::GuiScrollBar*>(bar), count);
}
void* CallGetProvider(void* manager)
{
    return static_cast<ui::GuiManager*>(manager)->VariableProvider();
}
void CallSetContext(void* manager, int32_t index, int32_t value)
{
    static_cast<ui::GuiManager*>(manager)->SetContext(index, value);
}
// What the six row-data procedures reach besides the frame: the scroll bar sync, building state and the staff list.
uint8_t CallIsOpen(void* building)
{
    return object::building_api::IsOpen(static_cast<const object::Building*>(building));
}

// The game side of gui_tables.h. Virtual slots are the common widget interface.
struct GameEnv {
    uint32_t& ActiveFilter() { return game::GetContext().guiTableActiveFilter; }
    const ui::GuiTableFilterDefinition* Definitions()
    {
        static_assert(std::size(gui_data::kFilterDefinitions) == gui_tables::kFilterDefinitionCount);
        return gui_data::kFilterDefinitions;
    }
    std::span<const ui::GuiTableHeadingKey> HeadingKeys() { return gui_content::Headings(); }
    uint32_t GuestFilterSelection() { return static_cast<uint32_t>(game::GetGuiManager()->m_context[1]); }
    uint32_t BuildingFilterSelection() { return static_cast<uint32_t>(game::GetGuiManager()->m_context[2]); }

    void* Alloc(size_t bytes) { return ::operator new(bytes); }
    template<class T> T* NewRecord() { return NewGuiRecord<T>(); }
    void Free(void* block) { CallDelete(block); }
    void FreeSmall(void* block, uint32_t) { ::operator delete(block); }

    Bar* ConstructBar(
            void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t minBits, uint32_t maxBits, const char* key, int32_t kind)
    {
        return static_cast<Bar*>(static_cast<void*>(GuiBarConstruct(static_cast<ui::GuiBar*>(memory), x0, y0, x1, y1,
                std::bit_cast<float>(minBits), std::bit_cast<float>(maxBits), key, kind)));
    }
    Image* ConstructImage(void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* key)
    {
        return static_cast<Image*>(
                static_cast<void*>(GuiVariableImageConstruct(static_cast<ui::GuiVariableImage*>(memory), x0, y0, x1, y1, key)));
    }
    void BaseSetVisible(Widget& w, bool visible) { w.SetVisibleBase(visible); }
    void BaseSetOrigin(Widget& w, int32_t dx, int32_t dy) { w.SetOriginBase(dx, dy); }
    void BaseTranslate(Widget& w, int32_t dx, int32_t dy) { w.TranslateBase(dx, dy); }
    void BaseInvalidate(Widget& w) { w.InvalidateBase(); }
    void TrackTooltip(Widget& w, int32_t x, int32_t y, int32_t state) { w.TrackTooltip(x, y, state); }
    void SetPosition(Widget& w, int32_t x, int32_t y) { w.SetPosition(x, y); }

    void DeleteWidget(Widget& w) { gui::DestroyWidget(w); }
    void DrawWidget(Widget& w) { w.Draw(); }
    void SetOrigin(Widget& w, int32_t dx, int32_t dy) { w.SetOrigin(dx, dy); }
    void Invalidate(Widget& w) { w.Invalidate(); }
    void DrawWidgetOverlay(Widget& w) { w.DrawOverlay(); }
    void SetVisible(Widget& w, bool visible) { w.SetVisible(visible ? 1u : 0u); }
    void Translate(Widget& w, int32_t dx, int32_t dy) { w.Translate(dx, dy); }
    void OnMouseLeft(Widget& w, int32_t x, int32_t y, int32_t state) { w.MouseLeft(x, y, state); }

    // The text mode of the filter cell: the provider's virtual `GetVariableText(key, out)` writes the text, then it is drawn black over the cell.
    void DrawVariableText(int32_t font, void* provider, const char* key, int32_t x, int32_t y, int32_t width, int32_t height)
    {
        Renderer* renderer = game::GetGuiManager()->m_textRenderers[font];
        Char16 text[0xfc];
        text[0] = 0;
        provider_virtual::GetVariableText(provider, key, text);
        CallSetAlign(renderer, 0);
        CallSetOutlined(renderer, 0);
        CallSetColor(renderer, 0);
        CallSetRect(renderer, x, y, width, height);
        CallDrawWrapped(renderer, 0, 0, text);
    }

    // ---- row methods (gui_table_rows.h) ----
    void* AllocSmall(size_t bytes) { return ::operator new(bytes); }
    void UpdateWidget(Widget& w, float dt) { w.Update(dt); }
    bool WasClicked(Widget& w) { return w.WasClicked() != 0; }
    void SetTooltip(Widget& w, const char* tip) { w.SetTooltip(tip); }
    // The row's object (a world object): its OnSelect(1, 1) selects it and centres the camera.
    void OpenObject(void* object) { provider_virtual::Select(object, 1, 1); }
    void* FindObject(int32_t id) { return CallGetObject(game::GetObjectManager(), id); }
    int32_t StaffHomeId(void* staff) { return static_cast<object::Staff*>(staff)->m_HomeID; }
    void IncrementShiftHours(void* staff) { CallIncrement(staff); }
    void DecrementShiftHours(void* staff) { CallDecrement(staff); }
    int32_t ThoughtObjectId(void* building, int32_t row) { return CallThoughtId(building, row); }
    void SetCellFilter(Cell& cell, uint32_t filter) { filter_cell::SetFilter(&cell, filter); }
    // The plain cell has no font of its own: the draw takes the row's y and the provider only.
    void DrawCell(Cell& cell, int32_t y, void* provider, int32_t) { filter_cell::Draw(&cell, y, provider); }
    void DrawCellOverlay(Cell& cell, int32_t y, void* provider, int32_t font) { filter_cell::DrawOverlay(&cell, y, provider, font); }

    const char* ButtonResource(int index) { return gui_content::ButtonResource(index); }
    ui::GuiButton* ConstructButton(void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* image)
    {
        const char* none = gui_data::kButtonNoText;
        const char* noAction = gui_data::kButtonNoAction;
        return button_facade::Construct(static_cast<ui::GuiButton*>(memory), x0, y0, x1, y1, image, none, noAction, noAction);
    }
    ui::GuiNumberAdjust* ConstructNumberAdjust(
            void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t kind, int32_t font, const char* key)
    {
        return number_adjust_facade::Construct(static_cast<ui::GuiNumberAdjust*>(memory), x0, y0, x1, y1, kind, font, key, 0, 0);
    }

    void* TextRenderer(int32_t font) { return game::GetGuiManager()->m_textRenderers[font]; }
    void GetText(void* provider, const char* key, Char16* out) { provider_virtual::GetVariableText(provider, key, out); }
    void SetAlign(void* r, int32_t align) { CallSetAlign(static_cast<Renderer*>(r), align); }
    void SetOutlined(void* r, int32_t outlined) { CallSetOutlined(static_cast<Renderer*>(r), static_cast<uint32_t>(outlined)); }
    void SetColor(void* r, uint32_t rgb) { CallSetColor(static_cast<Renderer*>(r), rgb); }
    void SetRect(void* r, int32_t x, int32_t y, int32_t w, int32_t h) { CallSetRect(static_cast<Renderer*>(r), x, y, w, h); }
    void Truncate(void* r, Char16* text, int32_t width) { CallTruncate(static_cast<Renderer*>(r), text, width); }
    void DrawWrapped(void* r, int32_t x, int32_t y, const Char16* text) { CallDrawWrapped(static_cast<Renderer*>(r), x, y, text); }
    // The selected-row highlight: the rectangle is {left, top, right, bottom} in the 16-bit renderer's colour.
    void FillRect(int32_t left, int32_t top, int32_t right, int32_t bottom)
    {
        void* renderer = game::GetRenderer();
        int32_t rect[4] = {left, top, right, bottom};
        const uint32_t color =
                obl::ui::renderer::PackColor(renderer, gui_tables::kSelectionRed, gui_tables::kSelectionGreen, gui_tables::kSelectionBlue);
        CallFillRect(renderer, rect, color);
    }

    // ---- table frame (gui_table_frame.h) ----
    uint8_t HitTest(Widget& w, int32_t x, int32_t y) { return w.HitTest(x, y); }
    uint8_t MouseWheel(Widget& w, int32_t x, int32_t y, int32_t delta) { return w.MouseWheel(x, y, delta); }
    uint8_t MouseLeft(Widget& w, int32_t x, int32_t y, int32_t state) { return w.MouseLeft(x, y, state); }
    void RefreshTable(Table& table) { table.Refresh(); }
    ui::GuiScrollBar* ConstructScrollBar(
            void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t top, uint32_t count, uint32_t visibleRows, uint32_t id)
    {
        return static_cast<ui::GuiScrollBar*>(CallBarConstruct(memory, x0, y0, x1, y1, top, count, visibleRows, id));
    }
    void ScrollBarSetItemCount(ui::GuiScrollBarData& bar, uint32_t count) { CallBarSetItemCount(&bar, count); }
    void SetContext(int32_t index, int32_t value) { CallSetContext(game::GetGuiManager(), index, value); }
    void* VariableProvider() { return CallGetProvider(game::GetGuiManager()); }
    object::Manager* Objects() { return game::GetObjectManager(); }
    ui::GuiManager& Gui() { return *game::GetGuiManager(); }
    bool IsOpen(void* building) { return CallIsOpen(building) != 0; }
    std::vector<object::Base*> StaffList() { return object::object_manager::GetNonServiceStaffList(game::GetObjectManager()); }
    void SortGuests(void** begin, void** end, int key, bool reverse) { gui_table_sort::Guests(begin, end, key, reverse); }
    void SortBuildings(void** begin, void** end, int key, bool reverse) { gui_table_sort::Buildings(begin, end, key, reverse); }
    void SortStaffByTitle(void** begin, void** end) { gui_table_sort::StaffByTitle(begin, end); }
    void SortStaffById(void** begin, void** end)
    {
        gui_sort::ObjectIdSort(begin, end, [](void* row) { return static_cast<object::Base*>(row)->m_id; });
    }
    void Sync(Table& table) { table::SyncScrollAndSelection(&table); }
    void CollectRows(Table& table, int32_t kind, float dt)
    {
        switch (kind) {
        case gui_tables::Guests: table::UpdateGuests(&table, dt); break;
        case gui_tables::Buildings: table::UpdateBuildings(&table, dt); break;
        case gui_tables::Commodities: table::UpdateBuildingCommodities(&table, dt); break;
        case gui_tables::PowerGenerators: table::UpdatePowerGenerators(&table, dt); break;
        case gui_tables::ClosedPower: table::UpdateBuildingsClosedPower(&table, dt); break;
        default: table::UpdateStaff(&table, dt); break;
        }
    }
    void ResizeColumn(Column& column, int32_t count) { table_column::ResizeRowWidgets(&column, count); }
    void UpdateColumnObject(Column& column, float dt, void* object) { table_column::UpdateObjectRow(&column, dt, object); }
    void UpdateColumnIndexed(Column& column, float dt, void* provider, int32_t row)
    {
        table_column::UpdateIndexedRow(&column, dt, provider, row);
    }
    void DrawColumnRow(Column& column, void* provider, bool selected) { table_column::DrawRow(&column, provider, selected); }
    void DrawColumnObjectOverlay(Column& column, void* object, bool selected)
    {
        table_column::DrawObjectRowOverlay(&column, object, selected);
    }
    // The indexed overlay has no selection state: the row method does not take the flag.
    void DrawColumnIndexedOverlay(Column& column, void* provider, int32_t row, bool)
    {
        table_column::DrawIndexedRowOverlay(&column, provider, row);
    }
};

} // namespace

namespace filter_cell {
// The GuiTableFilterCell methods that other code calls (src/game/ui/gui_tables.h). Construct, Destruct, Translate, SetOrigin and
// GetHeadingKey have no caller outside the table code, which runs their logic directly.
void SetFilter(Cell* cell, uint32_t filter)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::SetFilter(*cell, filter, env);
}
void Draw(Cell* cell, int32_t y, void* provider)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::DrawFilterCell(*cell, y, provider, env);
}
void DrawOverlay(Cell* cell, int32_t y, void* provider, int32_t font)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::DrawFilterCellOverlay(*cell, y, provider, font, env);
}
} // namespace filter_cell

namespace table_column {
// The per-row methods of GuiTableColumn (src/game/ui/gui_table_rows.h). The indexed overlay takes no selection: the row method
// does not use it, so the facade does not take it either.
void UpdateObjectRow(Column* column, float dt, void* object)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::UpdateObjectRow(*column, dt, object, env);
}
void UpdateIndexedRow(Column* column, float dt, void* provider, int32_t row)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::UpdateIndexedRow(*column, dt, provider, row, env);
}
void DrawRow(Column* column, void* provider, bool selected)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::DrawRow(*column, provider, selected, env);
}
void DrawObjectRowOverlay(Column* column, void* object, bool selected)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::DrawObjectRowOverlay(*column, object, selected, env);
}
void DrawIndexedRowOverlay(Column* column, void* provider, int32_t row)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::DrawIndexedRowOverlay(*column, provider, row, env);
}
void ResizeRowWidgets(Column* column, int32_t count)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::ResizeRowWidgets(*column, count, env);
}

// The common widget slots of the column (the vtable entries forward to these: gui_tables_slots).
void SetVisible(Column* column, uint32_t visible)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::SetColumnVisible(*column, visible != 0, env);
}
void SetOrigin(Column* column, int32_t dx, int32_t dy)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::SetColumnOrigin(*column, dx, dy, env);
}
void Translate(Column* column, int32_t dx, int32_t dy)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::TranslateColumn(*column, dx, dy, env);
}
void Invalidate(Column* column)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::InvalidateColumn(*column, env);
}
uint8_t OnMouseLeft(Column* column, int32_t x, int32_t y, int32_t state)
{
    g_counter.Count();
    GameEnv env;
    return gui_tables::ColumnOnMouseLeft(*column, x, y, state, env);
}
} // namespace table_column

namespace table {
// The six row-data procedures (table::Update*): the frame's Update calls them through CollectRows.
#define OBL_COLLECT_FACADE(Facade, Collector)                                                                                              \
    void Facade(Table* table, float dt)                                                                                                    \
    {                                                                                                                                      \
        g_counter.Count();                                                                                                                 \
        GameEnv env;                                                                                                                       \
        gui_tables::Collector(*table, dt, env);                                                                                            \
    }
OBL_COLLECT_FACADE(UpdateGuests, CollectGuests)
OBL_COLLECT_FACADE(UpdateBuildings, CollectBuildings)
OBL_COLLECT_FACADE(UpdatePowerGenerators, CollectPowerGenerators)
OBL_COLLECT_FACADE(UpdateBuildingsClosedPower, CollectClosedPower)
OBL_COLLECT_FACADE(UpdateBuildingCommodities, CollectCommodities)
OBL_COLLECT_FACADE(UpdateStaff, CollectStaff)
#undef OBL_COLLECT_FACADE

// Group 5: the table frame. GuiTable's virtual functions forward to these, so each virtual call counts once.
ui::GuiTable* Construct(ui::GuiTable* table, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t barX0, int32_t barY0, int32_t barX1,
        int32_t barY1, const char* name, int32_t rowHeight, uint32_t limit, uint8_t autoSelect)
{
    return new (static_cast<void*>(table)) ui::GuiTable(x0, y0, x1, y1, barX0, barY0, barX1, barY1, name, rowHeight, limit, autoSelect);
}
void SyncScrollAndSelection(Table* table)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::SyncScrollAndSelection(*table, env);
}
void SetVisible(Table* table, bool visible)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::SetTableVisible(*table, visible, env);
}
uint8_t OnMouseWheel(Table* table, int32_t x, int32_t y, int32_t delta)
{
    g_counter.Count();
    GameEnv env;
    return gui_tables::TableOnMouseWheel(*table, x, y, delta, env);
}
uint8_t OnMouseLeft(Table* table, int32_t x, int32_t y, int32_t state)
{
    g_counter.Count();
    GameEnv env;
    return gui_tables::TableOnMouseLeft(*table, x, y, state, env);
}
void Refresh(Table* table)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::RefreshTable(*table, env);
}
void Update(Table* table, float dt)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::UpdateTable(*table, dt, env);
}
void Invalidate(Table* table)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::InvalidateTable(*table, env);
}
void Draw(Table* table)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::DrawTable(*table, env);
}
void DrawOverlay(Table* table)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::DrawTableOverlay(*table, env);
}
void AddChild(Table* table, Column* child)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::AddTableColumn(*table, child, env);
}
void Translate(Table* table, int32_t dx, int32_t dy)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::TranslateTable(*table, dx, dy, env);
}
void SetOrigin(Table* table, int32_t dx, int32_t dy)
{
    g_counter.Count();
    GameEnv env;
    gui_tables::SetTableOrigin(*table, dx, dy, env);
}
} // namespace table

bool RegisterGuiTables()
{
    g_shiftUpCommand.SetDriver(DriveCommandShift<true>);
    g_shiftDownCommand.SetDriver(DriveCommandShift<false>);
    return true;
}
} // namespace obl::gui

namespace obl::ui {

GuiTableColumn::GuiTableColumn(int x0, int y0, int x1, int y1, const char* key, int font, uint32_t color, int flag, int xOffset)
    : GuiLeaf(x0, y0, x1, y1)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_tables::ConstructColumn(this, x0, y0, x1, y1, key, font, color, flag, xOffset, env);
}

GuiTableColumn::~GuiTableColumn()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_tables::DestructColumn(*this, env);
}

void GuiTableColumn::SetVisible(uint32_t visible)
{
    gui::table_column::SetVisible(this, visible);
}

void GuiTableColumn::SetOrigin(int dx, int dy)
{
    gui::table_column::SetOrigin(this, dx, dy);
}

void GuiTableColumn::Translate(int dx, int dy)
{
    gui::table_column::Translate(this, dx, dy);
}

void GuiTableColumn::Invalidate()
{
    gui::table_column::Invalidate(this);
}

bool GuiTableColumn::MouseLeft(int x, int y, int state)
{
    return gui::table_column::OnMouseLeft(this, x, y, state) != 0;
}

GuiTable::GuiTable(int x0, int y0, int x1, int y1, int barX0, int barY0, int barX1, int barY1, const char* name, int rowHeight,
        uint32_t limit, uint8_t autoSelect)
    : GuiLeaf(x0, y0, x1, y1)
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_tables::ConstructTable(this, barX0, barY0, barX1, barY1, name, rowHeight, limit, autoSelect, env);
}

GuiTable::~GuiTable()
{
    gui::g_counter.Count();
    gui::GameEnv env;
    gui_tables::DestructTable(*this, env);
}

void GuiTable::SetVisible(uint32_t visible)
{
    gui::table::SetVisible(this, visible != 0);
}

bool GuiTable::MouseWheel(int x, int y, int delta)
{
    return gui::table::OnMouseWheel(this, x, y, delta) != 0;
}

bool GuiTable::MouseLeft(int x, int y, int state)
{
    return gui::table::OnMouseLeft(this, x, y, state) != 0;
}

void GuiTable::Refresh()
{
    gui::table::Refresh(this);
}

void GuiTable::Update(float dt)
{
    gui::table::Update(this, dt);
}

void GuiTable::Invalidate()
{
    gui::table::Invalidate(this);
}

void GuiTable::Draw()
{
    gui::table::Draw(this);
}

void GuiTable::DrawOverlay()
{
    gui::table::DrawOverlay(this);
}

void GuiTable::AddChild(GuiWidget* child)
{
    gui::table::AddChild(this, static_cast<GuiTableColumnData*>(child));
}

void GuiTable::Translate(int dx, int dy)
{
    gui::table::Translate(this, dx, dy);
}

void GuiTable::SetOrigin(int dx, int dy)
{
    gui::table::SetOrigin(this, dx, dy);
}

} // namespace obl::ui
