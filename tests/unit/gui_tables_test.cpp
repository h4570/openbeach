// GuiTableFilterCell and the simple GuiTableColumn methods (src/game/ui/gui_tables.h): filter selection, rectangles, kinds of column names,
// forwarding to the row widgets and the lifecycle, on plain memory.

#include "unit_test.h"
#include "gui_fake_records.h"
#include "leak_ok.h"

#include "game/ui/gui_table_rows.h"
#include "game/ui/gui_tables.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace obl::gui_tables;
using GuiBar = obl::ui::GuiBarData;
using obl::ui::GuiTableFilterCell;
using obl::ui::GuiTableFilterDefinition;
using obl::ui::GuiTableHeadingKey;
using GuiVariableImage = obl::ui::GuiVariableImageData;
using obl::ui::GuiWidget;

namespace {

GuiTableFilterDefinition g_definitions[kFilterDefinitionCount];
GuiTableHeadingKey g_headings[27];

struct FakeEnv {
    std::vector<std::string> log;
    uint32_t active = 0;
    uint32_t guest = 0, building = 0;
    std::vector<void*> freed;
    GuiBar bar{};
    GuiVariableImage image{};

    uint32_t& ActiveFilter() { return active; }
    const GuiTableFilterDefinition* Definitions() { return g_definitions; }
    std::span<const GuiTableHeadingKey> HeadingKeys() { return g_headings; }
    uint32_t GuestFilterSelection() { return guest; }
    uint32_t BuildingFilterSelection() { return building; }
    // The GUI records (`new T()` in the game) come from Alloc with the record's size.
    template<class T> T* NewRecord()
    {
        void* memory = Alloc(sizeof(T));
        return memory ? std::construct_at(static_cast<T*>(memory)) : nullptr;
    }
    void* Alloc(size_t bytes)
    {
        // The expectations use the Win32 record sizes; native pointers make the x64 records larger.
        size_t logged = bytes;
        if (bytes == sizeof(obl::ui::GuiButtonData)) logged = 128;
        if (bytes == sizeof(obl::ui::GuiNumberAdjustData)) logged = 112;
        log.push_back("alloc:" + std::to_string(logged));
        return LeakOk(std::calloc(1, bytes));
    }
    void Free(void* block)
    {
        log.push_back("free");
        freed.push_back(block);
    }
    void FreeSmall(void* block, uint32_t bytes)
    {
        log.push_back("small:" + std::to_string(bytes * 4 / sizeof(void*)));
        freed.push_back(block);
    }
    obl::ui::GuiBarData* ConstructBar(
            void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t lo, uint32_t hi, const char* key, int32_t kind)
    {
        log.push_back("bar:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1) + ":" +
                std::to_string(lo) + "," + std::to_string(hi) + ":" + key + ":" + std::to_string(kind));
        auto* b = static_cast<obl::ui::GuiBarData*>(memory);
        b->m_id = 77;
        return b;
    }
    obl::ui::GuiVariableImageData* ConstructImage(void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* key)
    {
        log.push_back(
                "image:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1) + ":" + key);
        return static_cast<obl::ui::GuiVariableImageData*>(memory);
    }
    static std::string Id(const GuiWidget& w) { return std::to_string(w.m_id); }
    void BaseSetVisible(GuiWidget&, bool v) { log.push_back(std::string("base-visible:") + (v ? "1" : "0")); }
    void BaseSetOrigin(GuiWidget&, int32_t dx, int32_t dy)
    {
        log.push_back("base-origin:" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void BaseTranslate(GuiWidget&, int32_t dx, int32_t dy)
    {
        log.push_back("base-translate:" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void BaseInvalidate(GuiWidget&) { log.push_back("base-invalidate"); }
    void TrackTooltip(GuiWidget&, int32_t x, int32_t y, int32_t s)
    {
        log.push_back("track:" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(s));
    }
    void SetPosition(GuiWidget& w, int32_t x, int32_t y)
    {
        log.push_back("pos:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y));
    }
    void DeleteWidget(GuiWidget& w) { log.push_back("delete:" + Id(w)); }
    void DrawWidget(GuiWidget& w) { log.push_back("draw:" + Id(w)); }
    void SetOrigin(GuiWidget& w, int32_t dx, int32_t dy)
    {
        log.push_back("origin:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void Invalidate(GuiWidget& w) { log.push_back("invalidate:" + Id(w)); }
    void DrawWidgetOverlay(GuiWidget& w) { log.push_back("overlay:" + Id(w)); }
    void SetVisible(GuiWidget& w, bool v) { log.push_back("visible:" + Id(w) + ":" + (v ? "1" : "0")); }
    void Translate(GuiWidget& w, int32_t dx, int32_t dy)
    {
        log.push_back("translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void OnMouseLeft(GuiWidget& w, int32_t x, int32_t y, int32_t s)
    {
        log.push_back("mouse:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(s));
    }
    void DrawVariableText(int32_t font, void* provider, const char* key, int32_t x, int32_t y, int32_t w, int32_t h)
    {
        log.push_back("text:" + std::to_string(font) + ":" + (provider ? "p" : "-") + ":" + key + ":" + std::to_string(x) + "," +
                std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h));
    }

    // ---- row methods (gui_table_rows.h) ----
    bool clicked = false;
    int nextId = 100;
    static std::string Num(int64_t v) { return std::to_string(v); }
    void UpdateWidget(GuiWidget& w, float dt) { log.push_back("update:" + Id(w) + ":" + Num(static_cast<int>(dt))); }
    bool WasClicked(GuiWidget&) { return clicked; }
    void SetTooltip(GuiWidget& w, const char* tip) { log.push_back("tip:" + Id(w) + ":" + tip); }
    void OpenObject(void* o) { log.push_back("open:" + Num(reinterpret_cast<intptr_t>(o))); }
    void* FindObject(int32_t id)
    {
        log.push_back("find:" + Num(id));
        return reinterpret_cast<void*>(static_cast<intptr_t>(1000 + id));
    }
    int32_t StaffHomeId(void*) { return 7; }
    void IncrementShiftHours(void* o) { log.push_back("inc:" + Num(reinterpret_cast<intptr_t>(o))); }
    void DecrementShiftHours(void* o) { log.push_back("dec:" + Num(reinterpret_cast<intptr_t>(o))); }
    int32_t ThoughtObjectId(void*, int32_t row) { return 40 + row; }
    void SetCellFilter(GuiTableFilterCell&, uint32_t filter) { log.push_back("setfilter:" + Num(filter)); }
    void DrawCell(GuiTableFilterCell&, int32_t y, void* provider, int32_t font)
    {
        log.push_back("cell:" + Num(y) + (provider ? ":p:" : ":-:") + Num(font));
    }
    void DrawCellOverlay(GuiTableFilterCell&, int32_t y, void* provider, int32_t font)
    {
        log.push_back("cell-overlay:" + Num(y) + (provider ? ":p:" : ":-:") + Num(font));
    }
    const char* ButtonResource(int index)
    {
        static const char* names[] = {"IMG_GOTO", "TIP_MORE", "TIP_DOWN", "TIP_UP", "TIP_HOME", "TIP_THOUGHT", "IMG_INC", "IMG_DEC"};
        return names[index];
    }
    static std::string Rect(int32_t a, int32_t b, int32_t c, int32_t d) { return Num(a) + "," + Num(b) + "," + Num(c) + "," + Num(d); }
    obl::ui::GuiButtonData* ConstructButton(void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char* image)
    {
        log.push_back("button:" + Rect(x0, y0, x1, y1) + ":" + image);
        auto* b = static_cast<obl::ui::GuiButtonData*>(memory);
        b->m_id = nextId++;
        return b;
    }
    obl::ui::GuiNumberAdjustData* ConstructNumberAdjust(
            void* memory, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t kind, int32_t font, const char* key)
    {
        log.push_back("adjust:" + Rect(x0, y0, x1, y1) + ":" + Num(kind) + ":" + Num(font) + ":" + key);
        auto* a = static_cast<obl::ui::GuiNumberAdjustData*>(memory);
        a->m_id = nextId++;
        return a;
    }
    void* AllocSmall(size_t bytes)
    {
        log.push_back("alloc-small:" + std::to_string(bytes * 4 / sizeof(void*))); // only pointer vectors come here; see FreeSmall
        return LeakOk(std::calloc(1, bytes));
    }
    void* TextRenderer(int32_t font)
    {
        log.push_back("renderer:" + Num(font));
        return reinterpret_cast<void*>(0x77);
    }
    void GetText(void*, const char* key, char16_t* out)
    {
        log.push_back(std::string("gettext:") + key);
        out[0] = 'A';
        out[1] = 0;
    }
    void SetAlign(void*, int32_t v) { log.push_back("align:" + Num(v)); }
    void SetOutlined(void*, int32_t v) { log.push_back("outlined:" + Num(v)); }
    void SetColor(void*, uint32_t v) { log.push_back("color:" + Num(v)); }
    void SetRect(void*, int32_t x, int32_t y, int32_t w, int32_t h) { log.push_back("rect:" + Rect(x, y, w, h)); }
    void Truncate(void*, char16_t*, int32_t width) { log.push_back("truncate:" + Num(width)); }
    void DrawWrapped(void*, int32_t x, int32_t y, const char16_t* text)
    {
        log.push_back("wrapped:" + Num(x) + "," + Num(y) + ":" + Num(text[0]));
    }
    void FillRect(int32_t l, int32_t t, int32_t r, int32_t b) { log.push_back("fill:" + Rect(l, t, r, b)); }
};

std::string Join(const std::vector<std::string>& v)
{
    std::string out;
    for (const auto& s : v) out += s + " ";
    return out;
}

void InitTables()
{
    for (int i = 0; i < kFilterDefinitionCount; ++i) g_definitions[i] = {static_cast<uint32_t>(i + 1), 1, "TEXT", 0, 0, 0};
    g_definitions[0] = {1, 0, "BAR", 0x11, 0x22, 5};
    g_definitions[16] = {17, 2, "NONE", 0, 0, 0};
    for (int i = 0; i < 27; ++i) g_headings[i] = {"HEAD", static_cast<uint32_t>(i + 1)};
    g_headings[3].m_key = "OB_X";
}

} // namespace

OBL_TEST_CASE("filter cell construct keeps mode and key, clears the global")
{
    FakeEnv env;
    env.active = 9;
    GuiTableFilterCell cell;
    std::memset(&cell, 0xab, sizeof cell);
    ConstructFilterCell(&cell, 1, 2, 3, 4, env);
    OBL_CHECK(cell.m_x0 == 1 && cell.m_baseY0 == 2 && cell.m_x1 == 3 && cell.m_baseY1 == 4);
    OBL_CHECK(cell.m_bar == nullptr);
    OBL_CHECK(cell.m_mode == static_cast<int32_t>(0xabababab));
    OBL_CHECK(env.active == 0);
}

OBL_TEST_CASE("filter cell SetFilter builds a bar, a text key or nothing")
{
    InitTables();
    FakeEnv env;
    GuiTableFilterCell cell{};
    ConstructFilterCell(&cell, 10, 20, 30, 40, env);
    SetFilter(cell, 1, env);
    OBL_CHECK(cell.m_mode == 0 && cell.m_bar != nullptr);
    OBL_CHECK(Join(env.log).find("bar:10,20,30,40:17,34:BAR:5") != std::string::npos);
    env.log.clear();
    SetFilter(cell, 1, env); // unchanged: nothing is rebuilt
    OBL_CHECK(env.log.empty());
    GuiBar* old = cell.m_bar;
    SetFilter(cell, 2, env);
    OBL_CHECK(cell.m_mode == 1 && cell.m_bar == nullptr);
    OBL_CHECK(std::string(cell.m_variableKey) == "TEXT");
    OBL_CHECK(Join(env.log) == "delete:77 ");
    std::free(old);
    SetFilter(cell, 17, env);
    OBL_CHECK(cell.m_mode == 2);
    SetFilter(cell, 99, env); // unrecognized filter
    OBL_CHECK(cell.m_mode == 2 && env.active == 99);
}

OBL_TEST_CASE("filter cell draws the bar or the text")
{
    InitTables();
    FakeEnv env;
    GuiBar bar{};
    bar.m_id = 5;
    bar.m_x0 = 7;
    GuiTableFilterCell cell{};
    ConstructFilterCell(&cell, 10, 20, 30, 40, env);
    cell.m_bar = &bar;
    cell.m_mode = 0;
    int provider = 0;
    DrawFilterCell(cell, 99, &provider, env);
    OBL_CHECK(Join(env.log) == "pos:5:7,99 invalidate:5 draw:5 ");
    OBL_CHECK(bar.m_provider == &provider);
    env.log.clear();
    DrawFilterCellOverlay(cell, 99, &provider, 3, env);
    OBL_CHECK(Join(env.log) == "pos:5:7,99 invalidate:5 overlay:5 ");
    env.log.clear();
    cell.m_mode = 1;
    cell.m_variableKey = "K";
    DrawFilterCell(cell, 99, &provider, env);
    OBL_CHECK(env.log.empty()); // the text is drawn only by the overlay
    DrawFilterCellOverlay(cell, 99, &provider, 3, env);
    OBL_CHECK(Join(env.log) == "text:3:p:K:10,99,20,15 ");
    env.log.clear();
    cell.m_mode = 2;
    DrawFilterCellOverlay(cell, 99, &provider, 3, env);
    cell.m_mode = 0;
    cell.m_bar = nullptr;
    DrawFilterCell(cell, 99, &provider, env);
    OBL_CHECK(env.log.empty());
}

OBL_TEST_CASE("filter cell rectangles and heading key")
{
    InitTables();
    FakeEnv env;
    GuiBar bar{};
    bar.m_id = 5;
    GuiTableFilterCell cell{};
    ConstructFilterCell(&cell, 10, 20, 30, 40, env);
    cell.m_bar = &bar;
    TranslateFilterCell(cell, 2, 3, env);
    OBL_CHECK(cell.m_x0 == 12 && cell.m_x1 == 32 && cell.m_y0 == 23 && cell.m_y1 == 43);
    OBL_CHECK(Join(env.log) == "translate:5:2,3 ");
    SetFilterCellOrigin(cell, 100, 200);
    OBL_CHECK(cell.m_x0 == 110 && cell.m_x1 == 130 && cell.m_y0 == 220 && cell.m_y1 == 240);
    env.active = 4;
    OBL_CHECK(std::string(GetHeadingKey(env)) == "OB_X");
    env.active = 500;
    OBL_CHECK(GetHeadingKey(env) == nullptr);
}

OBL_TEST_CASE("column name prefixes decide the kind")
{
    int prefix = 0;
    OBL_CHECK(KindOfName("VAR_CASH", prefix) == ColumnVariable && prefix == 4);
    OBL_CHECK(KindOfName("VARIMG_FACE", prefix) == ColumnImage && prefix == 7);
    OBL_CHECK(KindOfName("FILTER_GUEST", prefix) == ColumnFilter && prefix == 7);
    OBL_CHECK(KindOfName("NUMADJ_HOURS", prefix) == ColumnNumberAdjust && prefix == 7);
    OBL_CHECK(KindOfName("BUT_GOTO", prefix) == ColumnButtons && prefix == 4);
    OBL_CHECK(KindOfName("VARX", prefix) == ColumnButtons);
}

OBL_TEST_CASE("column construct builds the picture and the filter cell")
{
    FakeEnv env;
    alignas(8) unsigned char bytes[sizeof(obl::ui::GuiTableColumnData)];
    void* storage = bytes;
    auto* column = std::construct_at(static_cast<obl::ui::GuiTableColumnData*>(storage));
    ConstructColumn(column, 10, 20, 110, 40, "VARIMG_FACE", 4, 0xff, 7, 3, env);
    OBL_CHECK(column->m_kind == ColumnImage && column->m_image != nullptr && column->m_filterCell == nullptr);
    OBL_CHECK(std::string(column->m_name) == "FACE");
    OBL_CHECK(column->m_font == 4 && column->m_color == 0xffu && column->m_flag == 7 && column->m_xOffset == 3);
    OBL_CHECK(column->m_type == kColumnType);
    OBL_CHECK(Join(env.log).find("image:13,20,110,40:FACE") != std::string::npos);
    std::free(column->m_name);
    std::free(column->m_image);
    column = std::construct_at(static_cast<obl::ui::GuiTableColumnData*>(storage));

    env.log.clear();
    ConstructColumn(column, 10, 20, 110, 40, "FILTER_GUEST", 0, 0, 0, 5, env);
    OBL_CHECK(column->m_kind == ColumnFilter && column->m_filterCell != nullptr && column->m_image == nullptr);
    OBL_CHECK(column->m_filterCell->m_x0 == 15 && column->m_filterCell->m_y1 == 33 && column->m_filterCell->m_x1 == 110);
    OBL_CHECK(std::string(column->m_name) == "GUEST");
    std::free(column->m_name);
    std::free(column->m_filterCell);
    column = std::construct_at(static_cast<obl::ui::GuiTableColumnData*>(storage));

    ConstructColumn(column, 0, 0, 1, 1, "BUT_GOTO", 0, 0, 0, 0, env);
    OBL_CHECK(column->m_kind == ColumnButtons && std::string(column->m_name) == "GOTO");
    std::free(column->m_name);
}

namespace {
struct ColumnFixture {
    FakeEnv env;
    GuiWidget widgets[4]{};
    void* buttons[3]{};
    void* adjusts[2]{};
    obl::ui::GuiTableColumnData column{};
    ColumnFixture(int kind)
    {
        for (int i = 0; i < 4; ++i) widgets[i].m_id = i + 1;
        buttons[0] = &widgets[0];
        buttons[1] = &widgets[1];
        adjusts[0] = &widgets[2];
        adjusts[1] = &widgets[3];
        column.m_kind = kind;
        column.m_buttons = {buttons, buttons + 2, buttons + 3};
        column.m_numberAdjusts = {adjusts, adjusts + 2, adjusts + 2};
        column.m_rowWidgets = 4;
    }
};
} // namespace

OBL_TEST_CASE("column ClearRowWidgets deletes buttons, hides and deletes adjusters, keeps the storage")
{
    ColumnFixture f(ColumnButtons);
    ClearRowWidgets(f.column, f.env);
    OBL_CHECK(Join(f.env.log) == "delete:1 delete:2 visible:3:0 delete:3 visible:4:0 delete:4 ");
    OBL_CHECK(f.column.m_buttons.m_end == f.column.m_buttons.m_begin && f.column.m_buttons.m_capacity == f.buttons + 3);
    OBL_CHECK(f.column.m_numberAdjusts.m_end == f.column.m_numberAdjusts.m_begin && f.column.m_rowWidgets == 0);
}

OBL_TEST_CASE("column origin and translate follow the kind")
{
    ColumnFixture f(ColumnButtons);
    SetColumnOrigin(f.column, 1, 2, f.env);
    OBL_CHECK(Join(f.env.log) == "base-origin:1,2 origin:1:1,2 origin:2:1,2 ");
    f.env.log.clear();
    f.column.m_kind = ColumnNumberAdjust;
    TranslateColumn(f.column, 3, 4, f.env);
    OBL_CHECK(Join(f.env.log) == "base-translate:3,4 translate:3:3,4 translate:4:3,4 ");
    f.env.log.clear();
    f.column.m_kind = ColumnVariable;
    SetColumnOrigin(f.column, 1, 2, f.env);
    TranslateColumn(f.column, 3, 4, f.env);
    OBL_CHECK(Join(f.env.log) == "base-origin:1,2 base-translate:3,4 ");
    f.env.log.clear();
    GuiTableFilterCell cell{};
    cell.m_baseX0 = 5;
    f.column.m_kind = ColumnFilter;
    f.column.m_filterCell = &cell;
    SetColumnOrigin(f.column, 10, 0, f.env);
    OBL_CHECK(cell.m_x0 == 15);
    TranslateColumn(f.column, 1, 1, f.env);
    OBL_CHECK(cell.m_x0 == 16);
}

OBL_TEST_CASE("column SetVisible picks the panel filter for FILTER_ columns")
{
    InitTables();
    ColumnFixture f(ColumnFilter);
    GuiTableFilterCell cell{};
    f.column.m_filterCell = &cell;
    f.env.guest = 2;
    f.env.building = 1;
    char guest[] = "GUEST", building[] = "BUILDING", other[] = "OTHER";
    f.column.m_name = guest;
    SetColumnVisible(f.column, true, f.env);
    OBL_CHECK(f.env.active == 2);
    f.column.m_name = building;
    SetColumnVisible(f.column, true, f.env);
    OBL_CHECK(f.env.active == 1);
    f.column.m_name = other;
    f.env.active = 77;
    SetColumnVisible(f.column, true, f.env);
    OBL_CHECK(f.env.active == 77);
    f.env.log.clear();
    SetColumnVisible(f.column, false, f.env);
    OBL_CHECK(Join(f.env.log).find("base-visible:0 delete:1") == 0);
    OBL_CHECK(f.column.m_rowWidgets == 0);
}

OBL_TEST_CASE("column invalidate and mouse reach every row widget and never consume")
{
    ColumnFixture f(ColumnButtons);
    InvalidateColumn(f.column, f.env);
    OBL_CHECK(Join(f.env.log) == "base-invalidate invalidate:1 invalidate:2 invalidate:3 invalidate:4 ");
    f.env.log.clear();
    OBL_CHECK(!ColumnOnMouseLeft(f.column, 5, 6, 2, f.env));
    OBL_CHECK(Join(f.env.log) == "track:5,6,2 mouse:1:5,6,2 mouse:2:5,6,2 mouse:3:5,6,2 mouse:4:5,6,2 ");
}

OBL_TEST_CASE("column destruct frees the owned pieces and the vector storage by size")
{
    ColumnFixture f(ColumnVariable);
    GuiTableFilterCell cell{};
    GuiVariableImage image{};
    image.m_id = 9;
    char name[] = "N";
    f.column.m_name = name;
    f.column.m_filterCell = &cell;
    f.column.m_image = &image;
    DestructColumn(f.column, f.env);
    OBL_CHECK(Join(f.env.log) ==
            "base-visible:0 delete:1 delete:2 visible:3:0 delete:3 visible:4:0 delete:4 delete:9 free free small:8 small:12 ");
    f.env.log.clear();
    // storage above 0x80 bytes goes to operator delete
    std::vector<void*> big(40);
    f.column.m_buttons = {big.data(), big.data(), big.data() + 40};
    f.column.m_numberAdjusts = {};
    f.column.m_filterCell = nullptr;
    f.column.m_image = nullptr;
    DestructColumn(f.column, f.env);
    OBL_CHECK(Join(f.env.log) == "base-visible:0 free free ");
}

// ---- row methods ------------------------------------------------------------------------------------------------------------------

namespace {
using obl::ui::GuiNumberAdjustData;
struct RowRig {
    FakeEnv env;
    obl::ui::GuiTableColumnData column{};
    GuiWidget buttons[3]{};
    GuiNumberAdjustData adjusts[3]{};
    void* buttonPointers[3]{};
    void* adjustPointers[3]{};
    char name[32]{};
    GuiTableFilterCell cell{};
    GuiVariableImage image{};
    RowRig(int kind, const char* columnName)
    {
        std::strcpy(name, columnName);
        for (int i = 0; i < 3; ++i) {
            buttons[i].m_id = i + 1;
            adjusts[i].m_id = i + 11;
            buttonPointers[i] = &buttons[i];
            adjustPointers[i] = &adjusts[i];
        }
        image.m_id = 21;
        column.m_name = name;
        column.m_kind = kind;
        column.m_buttons = {buttonPointers, buttonPointers + 3, buttonPointers + 3};
        column.m_numberAdjusts = {adjustPointers, adjustPointers + 3, adjustPointers + 3};
        column.m_filterCell = &cell;
        column.m_image = &image;
        column.m_x0 = 10;
        column.m_y0 = 20;
        column.m_x1 = 110;
        column.m_y1 = 40;
        column.m_rowHeight = 18;
        column.m_rowY = 36;
        column.m_font = 4;
        column.m_color = 0xabcdef;
        column.m_xOffset = 5;
        column.m_cursor = 1;
        env.guest = 3;
        env.building = 4;
    }
    std::string Log() const { return Join(env.log); }
};
} // namespace

OBL_TEST_CASE("UpdateObjectRow updates the cursor button and acts on a click")
{
    RowRig r(ColumnButtons, "GOTO");
    UpdateObjectRow(r.column, 2.0f, reinterpret_cast<void*>(500), r.env);
    OBL_CHECK(r.Log() == "update:2:2 ");
    OBL_CHECK(r.column.m_cursor == 2);
    r.env.log.clear();
    r.env.clicked = true;
    r.buttons[2].m_clicked = 1;
    UpdateObjectRow(r.column, 1.0f, reinterpret_cast<void*>(500), r.env);
    OBL_CHECK(r.Log() == "update:3:1 open:500 ");
    OBL_CHECK(r.buttons[2].m_clicked == 0 && r.column.m_cursor == 3);

    for (const char* n : {"GOTO_HOME", "STAFF_HOUR_DEC", "STAFF_HOUR_INC", "GOTO_THOUGHT"}) {
        RowRig s(ColumnButtons, n);
        s.env.clicked = true;
        UpdateObjectRow(s.column, 0.0f, reinterpret_cast<void*>(500), s.env);
        std::string expected = std::string(n) == "GOTO_HOME" ? "find:7 open:1007 "
                : std::string(n) == "STAFF_HOUR_DEC"         ? "dec:500 "
                : std::string(n) == "STAFF_HOUR_INC"         ? "inc:500 "
                                                             : "";
        OBL_CHECK(s.Log() == "update:2:0 " + expected);
    }
}

OBL_TEST_CASE("UpdateObjectRow keeps the panel filter and gives the adjuster its object")
{
    RowRig g(ColumnFilter, "GUEST");
    UpdateObjectRow(g.column, 0.0f, nullptr, g.env);
    OBL_CHECK(g.Log() == "setfilter:3 " && g.column.m_cursor == 2);
    RowRig b(ColumnFilter, "BUILDING");
    UpdateObjectRow(b.column, 0.0f, nullptr, b.env);
    OBL_CHECK(b.Log() == "setfilter:4 ");
    RowRig o(ColumnFilter, "OTHER");
    UpdateObjectRow(o.column, 0.0f, nullptr, o.env);
    OBL_CHECK(o.Log().empty() && o.column.m_cursor == 2);

    RowRig a(ColumnNumberAdjust, "NUM");
    UpdateObjectRow(a.column, 3.0f, reinterpret_cast<void*>(9), a.env);
    OBL_CHECK(a.adjusts[1].m_provider == reinterpret_cast<void*>(9) && a.Log() == "update:12:3 ");
    a.adjusts[2].m_provider = reinterpret_cast<void*>(5);
    UpdateObjectRow(a.column, 3.0f, nullptr, a.env); // a null object leaves the provider of the adjuster alone
    OBL_CHECK(a.adjusts[2].m_provider == reinterpret_cast<void*>(5));

    RowRig v(ColumnVariable, "VAR");
    UpdateObjectRow(v.column, 0.0f, nullptr, v.env);
    OBL_CHECK(v.Log().empty() && v.column.m_cursor == 2);
}

OBL_TEST_CASE("UpdateIndexedRow opens the thought of a clicked GOTO_THOUGHT button")
{
    RowRig r(ColumnButtons, "GOTO_THOUGHT");
    r.env.clicked = true;
    UpdateIndexedRow(r.column, 1.0f, reinterpret_cast<void*>(8), 6, r.env);
    OBL_CHECK(r.Log() == "update:2:1 find:46 open:1046 " && r.column.m_cursor == 2);
    RowRig other(ColumnButtons, "GOTO");
    other.env.clicked = true;
    UpdateIndexedRow(other.column, 1.0f, reinterpret_cast<void*>(8), 6, other.env);
    OBL_CHECK(other.Log() == "update:2:1 ");
    RowRig a(ColumnNumberAdjust, "NUM");
    UpdateIndexedRow(a.column, 1.0f, reinterpret_cast<void*>(8), 6, a.env);
    OBL_CHECK(a.adjusts[1].m_provider == reinterpret_cast<void*>(8) && a.column.m_cursor == 2);
    RowRig f(ColumnFilter, "GUEST");
    UpdateIndexedRow(f.column, 1.0f, nullptr, 0, f.env);
    OBL_CHECK(f.Log().empty() && f.column.m_cursor == 2);
}

OBL_TEST_CASE("DrawRow draws the widget of the cursor and advances the row")
{
    RowRig b(ColumnButtons, "GOTO");
    DrawRow(b.column, nullptr, true, b.env);
    OBL_CHECK(b.Log() == "draw:2 " && b.column.m_cursor == 2 && b.column.m_rowY == 54);
    RowRig a(ColumnNumberAdjust, "NUM");
    DrawRow(a.column, nullptr, false, a.env);
    OBL_CHECK(a.Log() == "draw:12 ");
    RowRig s(ColumnNumberAdjust, "NUM");
    DrawRow(s.column, nullptr, true, s.env);
    OBL_CHECK(s.Log() == "invalidate:12 draw:12 ");
    int provider = 0;
    RowRig f(ColumnFilter, "BUILDING");
    DrawRow(f.column, &provider, false, f.env);
    OBL_CHECK(f.Log() == "setfilter:4 cell:56:p:4 ");
    RowRig i(ColumnImage, "IMG");
    DrawRow(i.column, nullptr, true, i.env);
    OBL_CHECK(i.Log().empty() && i.column.m_rowY == 54);
}

OBL_TEST_CASE("DrawObjectRowOverlay fills a selected row and draws by kind")
{
    int object = 0;
    RowRig t(ColumnVariable, "VAR");
    DrawObjectRowOverlay(t.column, &object, true, t.env);
    OBL_CHECK(t.Log() ==
            "renderer:4 fill:10,56,110,73 gettext:VAR align:0 outlined:0 color:11259375 rect:10,56,100,18 truncate:100 wrapped:5,0:65 ");
    OBL_CHECK(t.column.m_rowY == 54 && t.column.m_cursor == 2);
    RowRig image(ColumnImage, "IMG");
    DrawObjectRowOverlay(image.column, &object, false, image.env);
    OBL_CHECK(image.image.m_object == &object && image.Log() == "renderer:4 pos:21:15,56 overlay:21 ");
    RowRig b(ColumnButtons, "GOTO");
    DrawObjectRowOverlay(b.column, &object, false, b.env);
    OBL_CHECK(b.Log() == "renderer:4 ");
    RowRig sb(ColumnButtons, "GOTO");
    DrawObjectRowOverlay(sb.column, &object, true, sb.env);
    OBL_CHECK(sb.Log() == "renderer:4 fill:10,56,110,73 invalidate:2 draw:2 ");
    RowRig f(ColumnFilter, "GUEST");
    DrawObjectRowOverlay(f.column, &object, false, f.env);
    OBL_CHECK(f.Log() == "renderer:4 setfilter:3 cell-overlay:56:p:4 ");
    RowRig a(ColumnNumberAdjust, "NUM");
    DrawObjectRowOverlay(a.column, &object, true, a.env);
    OBL_CHECK(a.Log() == "renderer:4 fill:10,56,110,73 invalidate:12 draw:12 overlay:12 " && a.adjusts[1].m_provider == &object);
}

OBL_TEST_CASE("DrawIndexedRowOverlay asks for <name>_<row> and cuts the text unless the flag is set")
{
    int provider = 0;
    RowRig t(ColumnVariable, "VAR");
    DrawIndexedRowOverlay(t.column, &provider, 7, t.env);
    OBL_CHECK(t.Log() == "renderer:4 align:0 outlined:0 color:11259375 rect:10,56,100,18 gettext:VAR_7 truncate:100 wrapped:0,0:65 ");
    OBL_CHECK(t.column.m_rowY == 54 && t.column.m_cursor == 2);
    RowRig flagged(ColumnVariable, "VAR");
    flagged.column.m_flag = 1;
    DrawIndexedRowOverlay(flagged.column, &provider, 7, flagged.env);
    OBL_CHECK(flagged.Log().find("truncate") == std::string::npos);
    RowRig a(ColumnNumberAdjust, "NUM");
    DrawIndexedRowOverlay(a.column, &provider, 7, a.env);
    OBL_CHECK(a.Log() == "overlay:12 ");
    RowRig other(ColumnButtons, "GOTO");
    DrawIndexedRowOverlay(other.column, &provider, 7, other.env);
    OBL_CHECK(other.Log().empty() && other.column.m_rowY == 54);
}

namespace {
struct ResizeRig {
    FakeEnv env;
    obl::ui::GuiTableColumnData column{};
    char name[32]{};
    ResizeRig(int kind, const char* columnName)
    {
        std::strcpy(name, columnName);
        column.m_name = name;
        column.m_kind = kind;
        column.m_x0 = 10;
        column.m_y0 = 20;
        column.m_x1 = 110;
        column.m_y1 = 40;
        column.m_rowHeight = 18;
        column.m_xOffset = 5;
        column.m_font = 3;
    }
    ~ResizeRig()
    {
        for (void** it = column.m_buttons.m_begin; it != column.m_buttons.m_end; ++it) std::free(*it);
        for (void** it = column.m_numberAdjusts.m_begin; it != column.m_numberAdjusts.m_end; ++it) std::free(*it);
        std::free(column.m_buttons.m_begin);
        std::free(column.m_numberAdjusts.m_begin);
    }
};
} // namespace

OBL_TEST_CASE("ResizeRowWidgets builds the named button for each row and grows the vector like STLport")
{
    ResizeRig r(ColumnButtons, "STAFF_HOUR_INC");
    ResizeRowWidgets(r.column, 3, r.env);
    OBL_CHECK(r.column.m_rowWidgets == 3);
    OBL_CHECK(Join(r.env.log) ==
            "alloc:128 button:15,20,110,38:IMG_INC tip:100:TIP_UP visible:100:1 alloc-small:4 "
            "alloc:128 button:15,38,110,56:IMG_INC tip:101:TIP_UP visible:101:1 alloc-small:8 small:4 "
            "alloc:128 button:15,56,110,74:IMG_INC tip:102:TIP_UP visible:102:1 alloc-small:16 small:8 ");
    OBL_CHECK(
            r.column.m_buttons.m_end - r.column.m_buttons.m_begin == 3 && r.column.m_buttons.m_capacity - r.column.m_buttons.m_begin == 4);
    OBL_CHECK(static_cast<obl::ui::GuiButtonData*>(r.column.m_buttons.m_begin[2])->m_repeat == 1);
    r.env.log.clear();
    ResizeRowWidgets(r.column, 3, r.env); // unchanged count: nothing happens
    OBL_CHECK(r.env.log.empty());
    void* second = r.column.m_buttons.m_begin[1];
    void* third = r.column.m_buttons.m_begin[2];
    ResizeRowWidgets(r.column, 1, r.env); // surplus widgets are deleted from the back
    OBL_CHECK(Join(r.env.log) == "delete:102 delete:101 ");
    OBL_CHECK(r.column.m_buttons.m_end - r.column.m_buttons.m_begin == 1);
    std::free(second);
    std::free(third);
}

OBL_TEST_CASE("ResizeRowWidgets picks image, tooltip and repeat by the button name")
{
    struct Case {
        const char* name;
        const char* expected;
        int repeat;
    };
    const Case cases[] = {
            {"GOTO", "alloc:128 button:15,20,110,38:IMG_GOTO tip:100:TIP_MORE visible:100:1 alloc-small:4 ", 0},
            {"GOTO_THOUGHT", "alloc:128 button:15,20,110,38:IMG_GOTO tip:100:TIP_THOUGHT visible:100:1 alloc-small:4 ", 0},
            {"GOTO_HOME", "alloc:128 button:15,20,110,38:IMG_GOTO tip:100:TIP_HOME visible:100:1 alloc-small:4 ", 0},
            {"STAFF_HOUR_DEC", "alloc:128 button:15,20,110,38:IMG_DEC tip:100:TIP_DOWN visible:100:1 alloc-small:4 ", 1},
    };
    for (const Case& c : cases) {
        ResizeRig r(ColumnButtons, c.name);
        ResizeRowWidgets(r.column, 1, r.env);
        OBL_CHECK(Join(r.env.log) == c.expected);
        OBL_CHECK(static_cast<obl::ui::GuiButtonData*>(r.column.m_buttons.m_begin[0])->m_repeat == c.repeat);
    }
    ResizeRig unrecognized(ColumnButtons, "SOMETHING");
    ResizeRowWidgets(unrecognized.column, 2, unrecognized.env);
    OBL_CHECK(unrecognized.env.log.empty() && unrecognized.column.m_rowWidgets == 2);
}

OBL_TEST_CASE("ResizeRowWidgets builds number adjusters with a key restarting at the loop index")
{
    ResizeRig r(ColumnNumberAdjust, "HOURS");
    ResizeRowWidgets(r.column, 2, r.env);
    OBL_CHECK(Join(r.env.log) ==
            "alloc:112 adjust:10,20,110,38:1:3:HOURS_0 visible:100:1 alloc-small:4 "
            "alloc:112 adjust:10,38,110,56:1:3:HOURS_1 visible:101:1 alloc-small:8 small:4 ");
    r.env.log.clear();
    r.column.m_flag = 1; // the other constructor variant
    ResizeRowWidgets(r.column, 3, r.env);
    OBL_CHECK(Join(r.env.log) == "alloc:112 adjust:10,56,110,74:0:3:HOURS_0 visible:102:1 alloc-small:16 small:8 ");
    r.env.log.clear();
    ResizeRowWidgets(r.column, 0, r.env);
    OBL_CHECK(Join(r.env.log) == "delete:102 delete:101 delete:100 " && r.column.m_numberAdjusts.m_end == r.column.m_numberAdjusts.m_begin);
}

OBL_TEST_CASE("ResizeRowWidgets moves big vectors through operator new")
{
    ResizeRig r(ColumnButtons, "GOTO");
    ResizeRowWidgets(r.column, 33, r.env);
    int largeAllocations = 0;
    for (const std::string& line : r.env.log) {
        if (line == "alloc:" + std::to_string(64 * sizeof(void*))) ++largeAllocations; // 64 native pointer slots
    }
    OBL_CHECK(largeAllocations == 1); // 32 -> 64 slots (256 bytes) is the only block above the pool limit
    OBL_CHECK(r.column.m_buttons.m_capacity - r.column.m_buttons.m_begin == 64);
    ResizeRig kindZero(ColumnVariable, "VAR");
    ResizeRowWidgets(kindZero.column, 5, kindZero.env);
    OBL_CHECK(kindZero.column.m_rowWidgets == 5 && kindZero.env.log.empty());
}
