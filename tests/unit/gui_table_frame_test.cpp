// The frame of GuiTable (src/common/gui_table_frame.h): constructor names, lifecycle, the scroll bar and selection rules and the per-frame loops, on
// plain memory with a recording Env.

#include "object_storage.h"
#include "unit_test.h"
#include "gui_fake_records.h"
#include "leak_ok.h"

#include "game/ui/gui_table_frame.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace obl::gui_tables;
using obl::ui::GuiPointerListNode;
using obl::ui::GuiScrollBarData;
using obl::ui::GuiWidget;

namespace {

struct FrameEnv {
    std::vector<std::string> log;
    GuiScrollBarData bar{}; // what the "allocation" hands out
    std::unique_ptr<obl::object::Building> provider = std::make_unique<obl::object::Building>();
    uint32_t collected = 0; // rows the row-data procedures "find"
    uint8_t hit = 1, wheel = 1, mouse = 0;
    bool barAllocFails = false;

    static std::string Num(int64_t v) { return std::to_string(v); }
    static std::string Id(const GuiWidget& w) { return Num(w.m_x0); }

    void* AllocSmall(size_t bytes)
    {
        log.push_back("small+:" + Num(bytes));
        return LeakOk(std::calloc(1, bytes < 16 ? 16 : bytes));
    }
    void* Alloc(size_t bytes)
    {
        log.push_back("alloc:" + Num(bytes));
        return std::calloc(1, bytes);
    }
    void FreeSmall(void*, uint32_t bytes) { log.push_back("small-:" + Num(bytes)); }
    void Free(void*) { log.push_back("free"); }
    void BaseSetVisible(GuiWidget&, bool v) { log.push_back(std::string("base-visible:") + (v ? "1" : "0")); }
    void BaseInvalidate(GuiWidget&) { log.push_back("base-invalidate"); }
    void BaseTranslate(GuiWidget&, int32_t dx, int32_t dy) { log.push_back("base-translate:" + Num(dx) + "," + Num(dy)); }
    void BaseSetOrigin(GuiWidget&, int32_t dx, int32_t dy) { log.push_back("base-origin:" + Num(dx) + "," + Num(dy)); }
    void DeleteWidget(GuiWidget& w) { log.push_back("delete:" + Id(w)); }
    void SetVisible(GuiWidget& w, bool v) { log.push_back("visible:" + Id(w) + ":" + (v ? "1" : "0")); }
    void Invalidate(GuiWidget& w) { log.push_back("invalidate:" + Id(w)); }
    void Translate(GuiWidget& w, int32_t dx, int32_t dy) { log.push_back("translate:" + Id(w) + ":" + Num(dx) + "," + Num(dy)); }
    void SetOrigin(GuiWidget& w, int32_t dx, int32_t dy) { log.push_back("origin:" + Id(w) + ":" + Num(dx) + "," + Num(dy)); }
    void DrawWidget(GuiWidget& w) { log.push_back("draw:" + Id(w)); }
    void DrawWidgetOverlay(GuiWidget& w) { log.push_back("overlay:" + Id(w)); }
    void UpdateWidget(GuiWidget& w, float dt) { log.push_back("update:" + Id(w) + ":" + Num(static_cast<int>(dt))); }
    uint8_t HitTest(GuiWidget& w, int32_t x, int32_t y)
    {
        log.push_back("hit:" + Id(w) + ":" + Num(x) + "," + Num(y));
        return hit;
    }
    uint8_t MouseWheel(GuiWidget& w, int32_t x, int32_t y, int32_t d)
    {
        log.push_back("wheel:" + Id(w) + ":" + Num(x) + "," + Num(y) + "," + Num(d));
        return wheel;
    }
    uint8_t MouseLeft(GuiWidget& w, int32_t x, int32_t y, int32_t s)
    {
        log.push_back("mouse:" + Id(w) + ":" + Num(x) + "," + Num(y) + "," + Num(s));
        return mouse;
    }
    void RefreshTable(Table&) { log.push_back("refresh"); }
    // The scroll bar (`new GuiScrollBar()` in the game) is `bar`; other records come from Alloc with their size.
    template<class T> T* NewRecord()
    {
        if constexpr (std::is_same_v<T, GuiScrollBarData>) {
            log.push_back("bar-new");
            return barAllocFails ? nullptr : &bar;
        } else {
            return std::construct_at(static_cast<T*>(Alloc(sizeof(T))));
        }
    }
    GuiScrollBarData* ConstructScrollBar(
            void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t top, uint32_t count, uint32_t visible, uint32_t id)
    {
        log.push_back("bar:" + Num(x0) + "," + Num(y0) + "," + Num(x1) + "," + Num(y1) + ":" + Num(top) + "," + Num(count) + "," +
                Num(visible) + "," + Num(id));
        auto* b = static_cast<GuiScrollBarData*>(memory);
        b->m_x0 = 90;
        b->m_top = top;
        return b;
    }
    void ScrollBarSetItemCount(GuiScrollBarData& b, uint32_t count)
    {
        log.push_back("bar-count:" + Num(count));
        b.m_top = 4;
    }
    void SetContext(int32_t index, int32_t value) { log.push_back("context:" + Num(index) + "," + Num(value)); }
    void* VariableProvider()
    {
        log.push_back("provider");
        return provider.get();
    }
    void CollectRows(Table& t, int32_t kind, float dt)
    {
        log.push_back("collect:" + Num(kind) + ":" + Num(static_cast<int>(dt)));
        t.m_count = collected;
    }
    void ResizeColumn(Column& c, int32_t count) { log.push_back("resize:" + Id(c) + ":" + Num(count)); }
    void UpdateColumnObject(Column& c, float, void* o) { log.push_back("updobj:" + Id(c) + ":" + Num(reinterpret_cast<intptr_t>(o))); }
    void UpdateColumnIndexed(Column& c, float, void* p, int32_t row)
    {
        log.push_back("updidx:" + Id(c) + ":" + Num(reinterpret_cast<intptr_t>(p)) + "," + Num(row));
    }
    void DrawColumnRow(Column& c, void* p, bool sel)
    {
        log.push_back("row:" + Id(c) + ":" + Num(reinterpret_cast<intptr_t>(p)) + "," + Num(sel));
    }
    void DrawColumnObjectOverlay(Column& c, void* o, bool sel)
    {
        log.push_back("objover:" + Id(c) + ":" + Num(reinterpret_cast<intptr_t>(o)) + "," + Num(sel));
    }
    void DrawColumnIndexedOverlay(Column& c, void* p, int32_t row, bool sel)
    {
        log.push_back("idxover:" + Id(c) + ":" + Num(reinterpret_cast<intptr_t>(p)) + "," + Num(row) + "," + Num(sel));
    }
};

std::string Join(const std::vector<std::string>& v)
{
    std::string out;
    for (const auto& s : v) out += s + " ";
    return out;
}

// A table with a sentinel-only column list, plus fake columns 1..n added to the list and a vector of row pointers.
struct Rig {
    FrameEnv env;
    Table table{};
    Column columns[3]{};
    GuiPointerListNode sentinel{}, nodes[3]{};
    void* rows[16]{};
    obl::object::Base objects[16]{};

    explicit Rig(int columnCount = 0)
    {
        for (int i = 0; i < 16; ++i) {
            rows[i] = &objects[i];
            objects[i].m_id = 100 + i;
        }
        table.m_rows = {rows, rows + 8, rows + 16};
        table.m_rowHeight = 10;
        table.m_limit = 4;
        table.m_x0 = 5;
        table.m_y0 = 50;
        table.m_selected = -1;
        sentinel.m_next = sentinel.m_prev = &sentinel;
        table.m_columns = &sentinel;
        GuiPointerListNode* tail = &sentinel;
        for (int i = 0; i < columnCount; ++i) {
            columns[i].m_x0 = i + 1;
            nodes[i].m_value = &columns[i];
            nodes[i].m_next = &sentinel;
            nodes[i].m_prev = tail;
            tail->m_next = &nodes[i];
            sentinel.m_prev = &nodes[i];
            tail = &nodes[i];
        }
    }
};

} // namespace

OBL_TEST_CASE("table constructor picks kind and row source by script name")
{
    struct Case {
        const char* name;
        int kind, source;
    };
    const Case cases[] = {{"GUESTS", 0, 1}, {"BUILDINGS", 1, 1}, {"COMMODITIES", 2, 0}, {"DEBUG_INFO", 7, 0}, {"SENT_INFO", 8, 0},
            {"BUILDING_THOUGHTS", 3, 0}, {"POWER_GENERATORS", 4, 1}, {"BUILDINGS_CLOSED_POWER", 5, 1}, {"STAFF_LIST", 6, 1}};
    for (const Case& c : cases) {
        FrameEnv env;
        Table t;
        ConstructTable(&t, 5, 6, 7, 8, c.name, 12, 9, 1, env);
        OBL_CHECK(t.m_dataKind == c.kind && t.m_rowSource == c.source);
        OBL_CHECK(t.m_barX0 == 5 && t.m_baseBarY1 == 8);
        OBL_CHECK(t.m_rowHeight == 12 && t.m_limit == 9 && t.m_autoSelect == 1 && t.m_selected == -1);
        OBL_CHECK(t.m_sortParam == 1 && t.m_refreshTimer == 4.0f && t.m_type == kTableType);
        OBL_CHECK(t.m_count == 0 && t.m_visibleRows == 0 && t.m_firstRow == 0 && t.m_scrollBar == nullptr && t.m_rows.m_begin == nullptr);
        OBL_CHECK(t.m_columns->m_next == t.m_columns && t.m_columns->m_prev == t.m_columns);
        std::free(t.m_columns);
    }
    FrameEnv env;
    Table t;
    ConstructTable(&t, 0, 0, 0, 0, "OTHER", 1, 1, 0, env);
    OBL_CHECK(t.m_dataKind == 0 && t.m_rowSource == 0); // an unrecognized name keeps the defaults
    std::free(t.m_columns);
}

OBL_TEST_CASE("table destruct returns nodes, sentinel and vector storage to the pool and deletes the scroll bar")
{
    Rig r(2);
    r.table.m_scrollBar = &r.env.bar;
    r.env.bar.m_x0 = 7;
    DestructTable(r.table, r.env);
    const std::string nodeFree = "small-:" + std::to_string(sizeof(GuiPointerListNode)) + " ";
    const std::string vectorFree = "small-:" + std::to_string(16 * sizeof(void*)) + " ";
    OBL_CHECK(Join(r.env.log) == "delete:7 " + nodeFree + nodeFree + nodeFree + vectorFree ||
            Join(r.env.log) == nodeFree + nodeFree + "delete:7 " + nodeFree + vectorFree);
    OBL_CHECK(r.sentinel.m_next == &r.sentinel);
}

OBL_TEST_CASE("table destruct frees a big row vector through operator delete")
{
    Rig r;
    void* big[40];
    r.table.m_rows = {big, big, big + 40};
    DestructTable(r.table, r.env);
    OBL_CHECK(Join(r.env.log) == "small-:" + std::to_string(sizeof(GuiPointerListNode)) + " free ");
}

OBL_TEST_CASE("SetVisible shows and refreshes, hiding forgets the rows")
{
    Rig r(2);
    r.table.m_count = 5;
    r.table.m_visibleRows = 4;
    SetTableVisible(r.table, true, r.env);
    OBL_CHECK(Join(r.env.log) == "base-visible:1 visible:1:1 visible:2:1 refresh invalidate:5 ");
    r.env.log.clear();
    r.table.m_scrollBar = &r.env.bar;
    r.env.bar.m_x0 = 9;
    SetTableVisible(r.table, false, r.env);
    OBL_CHECK(Join(r.env.log) == "base-visible:0 visible:9:0 visible:1:0 visible:2:0 ");
    OBL_CHECK(r.table.m_count == 0 && r.table.m_visibleRows == 0 && r.table.m_rows.m_end == r.table.m_rows.m_begin);
}

OBL_TEST_CASE("mouse wheel needs a bar and the cursor over the table or the bar")
{
    Rig r;
    OBL_CHECK(TableOnMouseWheel(r.table, 1, 2, 3, r.env) == 0 && r.env.log.empty());
    r.table.m_scrollBar = &r.env.bar;
    r.env.bar.m_x0 = 4;
    r.env.hit = 0;
    OBL_CHECK(TableOnMouseWheel(r.table, 1, 2, 3, r.env) == 0);
    OBL_CHECK(Join(r.env.log) == "hit:5:1,2 hit:4:1,2 ");
    r.env.log.clear();
    r.env.hit = 1;
    r.env.wheel = 7;
    OBL_CHECK(TableOnMouseWheel(r.table, 1, 2, -3, r.env) == 7);
    OBL_CHECK(Join(r.env.log) == "hit:5:1,2 wheel:4:1,2,-3 ");
}

OBL_TEST_CASE("a click goes to the bar and the columns until one consumes it")
{
    Rig r(3);
    r.table.m_scrollBar = &r.env.bar;
    r.env.bar.m_x0 = 8;
    r.env.mouse = 1;
    OBL_CHECK(TableOnMouseLeft(r.table, 10, 20, 1, r.env) == 1);
    OBL_CHECK(Join(r.env.log) == "mouse:8:10,20,1 "); // the bar consumed it: no column and no selection
}

OBL_TEST_CASE("a click on a row selects it by unsigned division and publishes the object id")
{
    Rig r(1);
    r.table.m_autoSelect = 1;
    r.table.m_count = 6;
    r.table.m_firstRow = 1;
    r.table.m_rowSource = 1;
    OBL_CHECK(TableOnMouseLeft(r.table, 10, 50 + 25, 1, r.env) == 0); // (75 - 50) / 10 + 1 = 3
    OBL_CHECK(r.table.m_selected == 3);
    OBL_CHECK(Join(r.env.log) == "mouse:1:10,75,1 hit:5:10,75 context:12,103 ");
    r.env.log.clear();
    TableOnMouseLeft(r.table, 10, 50 + 900, 1, r.env); // clamped to the last row
    OBL_CHECK(r.table.m_selected == 5 && r.env.log.back() == "context:12,105");
    r.env.log.clear();
    r.table.m_rowSource = 0; // one shared provider: the first element answers whatever the row
    TableOnMouseLeft(r.table, 10, 50 + 25, 1, r.env);
    OBL_CHECK(r.env.log.back() == "context:12,100");
    r.env.log.clear();
    TableOnMouseLeft(r.table, 10, 60, 2, r.env); // only the press (state 1) selects
    OBL_CHECK(Join(r.env.log) == "mouse:1:10,60,2 ");
    r.table.m_autoSelect = 0;
    r.env.log.clear();
    TableOnMouseLeft(r.table, 10, 60, 1, r.env);
    OBL_CHECK(Join(r.env.log) == "mouse:1:10,60,1 ");
}

OBL_TEST_CASE("above the table the unsigned row is huge and lands on the last row")
{
    Rig r;
    r.table.m_autoSelect = 1;
    r.table.m_count = 4;
    r.table.m_rowSource = 1;
    TableOnMouseLeft(r.table, 0, 40, 1, r.env); // y - y0 = -10
    OBL_CHECK(r.table.m_selected == 3);
}

OBL_TEST_CASE("Refresh collects every kind with eight seconds and resizes the columns")
{
    for (int kind : {0, 1, 2, 4, 5, 6}) {
        Rig r(2);
        r.table.m_dataKind = kind;
        r.env.collected = 9;
        RefreshTable(r.table, r.env);
        OBL_CHECK(Join(r.env.log) == "collect:" + std::to_string(kind) + ":8 resize:1:4 resize:2:4 invalidate:5 ");
        OBL_CHECK(r.table.m_visibleRows == 4);
    }
    Rig other(1);
    other.table.m_dataKind = 7;
    other.table.m_count = 2;
    RefreshTable(other.table, other.env);
    OBL_CHECK(Join(other.env.log) == "resize:1:2 invalidate:5 "); // kinds without a procedure keep the old count
}

OBL_TEST_CASE("a thoughts table keeps the current building as its single row")
{
    Rig r(1);
    r.table.m_dataKind = Thoughts;
    r.env.provider->m_ThoughtList.assign(3, nullptr);
    RefreshTable(r.table, r.env);
    OBL_CHECK(r.table.m_rows.m_end == r.table.m_rows.m_begin + 1);
    OBL_CHECK(r.table.m_rows.m_begin[0] == r.env.provider.get());
    OBL_CHECK(r.table.m_count == 3 && r.table.m_visibleRows == 3);
}

OBL_TEST_CASE("Update takes the first row from the bar and serves every column per visible row")
{
    Rig r(2);
    r.table.m_dataKind = Guests;
    r.table.m_rowSource = 1;
    r.table.m_scrollBar = &r.env.bar;
    r.env.bar.m_x0 = 9;
    r.env.bar.m_top = 2;
    r.env.collected = 10;
    r.columns[0].m_cursor = 5;
    r.columns[0].m_rowY = 7;
    UpdateTable(r.table, 0.5f, r.env);
    OBL_CHECK(Join(r.env.log) ==
            "collect:0:0 update:9:0 resize:1:4 updobj:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[2])) +
                    " updobj:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[3])) +
                    " updobj:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[4])) +
                    " updobj:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[5])) +
                    " resize:2:4 updobj:2:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[2])) +
                    " updobj:2:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[3])) +
                    " updobj:2:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[4])) +
                    " updobj:2:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[5])) + " ");
    OBL_CHECK(r.table.m_firstRow == 2 && r.columns[0].m_cursor == 0 && r.columns[0].m_rowY == 0);
    Rig shared(1);
    shared.table.m_dataKind = Commodities;
    shared.table.m_rowSource = 0;
    shared.env.collected = 2;
    UpdateTable(shared.table, 0.0f, shared.env);
    const std::string provider = std::to_string(reinterpret_cast<intptr_t>(shared.rows[0]));
    OBL_CHECK(Join(shared.env.log) == "collect:2:0 resize:1:2 updidx:1:" + provider + ",0 updidx:1:" + provider + ",1 ");
}

OBL_TEST_CASE("SyncScrollAndSelection creates, updates and drops the bar around the visible limit")
{
    Rig r;
    r.table.m_barX0 = 150;
    r.table.m_barY0 = 60;
    r.table.m_barX1 = 166;
    r.table.m_barY1 = 190;
    r.table.m_count = 9;
    r.table.m_firstRow = 3;
    SyncScrollAndSelection(r.table, r.env);
    OBL_CHECK(Join(r.env.log) == "bar-new bar:150,60,166,190:0,9,4,3 ");
    OBL_CHECK(r.table.m_scrollBar == &r.env.bar && r.table.m_firstRow == 0);
    r.env.log.clear();
    r.table.m_count = 12;
    SyncScrollAndSelection(r.table, r.env);
    OBL_CHECK(Join(r.env.log) == "bar-count:12 " && r.table.m_firstRow == 4);
    r.env.log.clear();
    r.table.m_count = 3;
    r.table.m_visibleRows = 77;
    SyncScrollAndSelection(r.table, r.env);
    OBL_CHECK(Join(r.env.log) == "delete:90 " && r.table.m_scrollBar == nullptr && r.table.m_firstRow == 0 && r.table.m_visibleRows == 3);
    Rig failing;
    failing.env.barAllocFails = true;
    failing.table.m_count = 9;
    SyncScrollAndSelection(failing.table, failing.env);
    OBL_CHECK(failing.table.m_scrollBar == nullptr);
}

OBL_TEST_CASE("the selection follows the data: first row when automatic, clamped, none when empty")
{
    Rig r;
    r.table.m_autoSelect = 1;
    r.table.m_count = 3;
    r.table.m_selected = -1;
    SyncScrollAndSelection(r.table, r.env);
    OBL_CHECK(r.table.m_selected == 0);
    r.table.m_selected = 9;
    SyncScrollAndSelection(r.table, r.env);
    OBL_CHECK(r.table.m_selected == 2);
    r.table.m_count = 0;
    SyncScrollAndSelection(r.table, r.env);
    OBL_CHECK(r.table.m_selected == -1);
    r.table.m_autoSelect = 0;
    r.table.m_count = 3;
    SyncScrollAndSelection(r.table, r.env);
    OBL_CHECK(r.table.m_selected == -1); // not automatic: -1 stays
}

OBL_TEST_CASE("Invalidate, Translate and SetOrigin reach the bar and every column")
{
    Rig r(2);
    r.table.m_scrollBar = &r.env.bar;
    r.env.bar.m_x0 = 9;
    InvalidateTable(r.table, r.env);
    OBL_CHECK(Join(r.env.log) == "base-invalidate invalidate:9 invalidate:1 invalidate:2 ");
    r.env.log.clear();
    r.table.m_barX0 = 10;
    r.table.m_barX1 = 30;
    r.table.m_barY0 = 5;
    r.table.m_barY1 = 25;
    TranslateTable(r.table, 2, 3, r.env);
    OBL_CHECK(Join(r.env.log) == "base-translate:2,3 translate:1:2,3 translate:2:2,3 translate:9:2,3 ");
    OBL_CHECK(r.table.m_barX0 == 12 && r.table.m_barX1 == 32 && r.table.m_barY0 == 8 && r.table.m_barY1 == 28);
    r.env.log.clear();
    r.table.m_baseBarX0 = 100;
    r.table.m_baseBarX1 = 120;
    r.table.m_baseBarY0 = 1;
    r.table.m_baseBarY1 = 9;
    SetTableOrigin(r.table, 4, 5, r.env);
    OBL_CHECK(Join(r.env.log) == "base-origin:4,5 origin:1:4,5 origin:2:4,5 ");
    OBL_CHECK(r.table.m_barX0 == 12); // with a bar the rectangle stays
    r.table.m_scrollBar = nullptr;
    SetTableOrigin(r.table, 4, 5, r.env);
    OBL_CHECK(r.table.m_barX0 == 104 && r.table.m_barX1 == 124 && r.table.m_barY0 == 6 && r.table.m_barY1 == 14);
}

OBL_TEST_CASE("Draw and DrawOverlay hand each visible row with its selection flag; an empty vector draws nothing")
{
    Rig r(1);
    r.table.m_visibleRows = 2;
    r.table.m_firstRow = 1;
    r.table.m_autoSelect = 1;
    r.table.m_selected = 2;
    r.table.m_rowSource = 1;
    r.table.m_scrollBar = &r.env.bar;
    r.env.bar.m_x0 = 9;
    DrawTable(r.table, r.env);
    OBL_CHECK(Join(r.env.log) ==
            "draw:9 resize:1:2 row:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[1])) +
                    ",0 row:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[2])) + ",1 ");
    r.env.log.clear();
    DrawTableOverlay(r.table, r.env);
    OBL_CHECK(Join(r.env.log) ==
            "overlay:9 resize:1:2 objover:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[1])) +
                    ",0 objover:1:" + std::to_string(reinterpret_cast<intptr_t>(r.rows[2])) + ",1 ");
    r.env.log.clear();
    r.table.m_rowSource = 0;
    r.table.m_autoSelect = 0;
    DrawTableOverlay(r.table, r.env);
    const std::string provider = std::to_string(reinterpret_cast<intptr_t>(r.rows[0]));
    OBL_CHECK(Join(r.env.log) == "overlay:9 resize:1:2 idxover:1:" + provider + ",1,0 idxover:1:" + provider + ",2,0 ");
    r.env.log.clear();
    r.table.m_rows.m_end = r.table.m_rows.m_begin;
    DrawTable(r.table, r.env);
    DrawTableOverlay(r.table, r.env);
    OBL_CHECK(r.env.log.empty());
}

OBL_TEST_CASE("AddChild appends a node and gives the column the row height")
{
    Rig r(1);
    r.table.m_rowHeight = 17;
    Column child{};
    AddTableColumn(r.table, &child, r.env);
    OBL_CHECK(Join(r.env.log) == "small+:" + std::to_string(sizeof(GuiPointerListNode)) + " ");
    OBL_CHECK(child.m_rowHeight == 17);
    OBL_CHECK(r.sentinel.m_prev->m_value == &child && r.sentinel.m_prev->m_next == &r.sentinel && r.sentinel.m_prev->m_prev == &r.nodes[0]);
    OBL_CHECK(r.nodes[0].m_next == r.sentinel.m_prev);
}
