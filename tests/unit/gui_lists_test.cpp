#include <cstdint>
// GuiListBox (src/game/ui/gui_lists.h): scrolling, row layout, forwarding to cells and the bar, mouse and row buttons.

#include "unit_test.h"

#include "game/ui/gui_lists.h"

#include <cstring>
#include <new>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace obl::gui_lists;
using obl::ui::GuiScrollBarData;
using obl::ui::GuiWidget;

namespace {

// Widgets are told apart by `m_id`; the environment answers for them and logs every call in order.
struct FakeEnv {
    std::vector<std::string> log;
    int answerId = -1; // widget whose mouse/click callbacks answer
    uint8_t answer = 5; // the raw byte they answer
    bool selfHit = false, barHit = false;
    bool ctrlLeft = false, ctrlRight = false;
    int rowTop = 0; // y of the list's rectangle for Bounds

    static std::string Id(const GuiWidget& w) { return std::to_string(w.m_id); }
    const int32_t* Bounds(GuiWidget& w, int32_t* out)
    {
        log.push_back("bounds:" + Id(w));
        out[0] = w.m_id * 10;
        out[1] = rowTop;
        out[2] = 0;
        out[3] = 0;
        return out;
    }
    void SetPosition(GuiWidget& w, int x, int y) { log.push_back("pos:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y)); }
    void SetTopRow(GuiScrollBarData&, uint32_t top) { log.push_back("top:" + std::to_string(top)); }
    void Update(GuiWidget& w, uint32_t dt) { log.push_back("update:" + Id(w) + ":" + std::to_string(dt)); }
    void Draw(GuiWidget& w) { log.push_back("draw:" + Id(w)); }
    void Invalidate(GuiWidget& w) { log.push_back("invalidate:" + Id(w)); }
    void Translate(GuiWidget& w, int dx, int dy)
    {
        log.push_back("translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    uint8_t HitTest(GuiWidget& w, int, int)
    {
        log.push_back("hit:" + Id(w));
        return w.m_id == 100 ? selfHit : barHit;
    }
    uint8_t OnMouseWheel(GuiWidget& w, int, int, int delta)
    {
        log.push_back("wheel:" + Id(w) + ":" + std::to_string(delta));
        return 0x81;
    }
    uint8_t OnMouseLeft(GuiWidget& w, int, int, int state)
    {
        log.push_back("left:" + Id(w) + ":" + std::to_string(state));
        return w.m_id == answerId ? answer : 0;
    }
    uint8_t IsClicked(GuiWidget& w)
    {
        log.push_back("clicked:" + Id(w));
        return w.m_id == answerId;
    }
    void BaseInvalidate(GuiWidget& w) { log.push_back("base-invalidate:" + Id(w)); }
    void BaseTranslate(GuiWidget& w, int dx, int dy)
    {
        log.push_back("base-translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    bool KeyHeld(int code)
    {
        log.push_back("key:" + std::to_string(code));
        return code == kCtrlLeft ? ctrlLeft : ctrlRight;
    }
    void SelectRow(ListBox& list, uint32_t row, int replace)
    {
        (void)list;
        log.push_back("select:" + std::to_string(row) + "," + std::to_string(replace));
    }

    std::map<const void*, std::string> label;
    bool failAlloc = false;
    alignas(8) unsigned char heap[1024]{};
    GuiScrollBarData madeBar{};
    std::string Name(const void* p)
    {
        auto it = label.find(p);
        return it == label.end() ? "?" : it->second;
    }
    void BaseSetVisible(GuiWidget& w, uint32_t v) { log.push_back("base-visible:" + Id(w) + "," + std::to_string(v)); }
    // The GUI records (`new T()` in the game) come from Alloc with the record's size.
    template<class T> T* NewRecord()
    {
        void* memory = Alloc(sizeof(T));
        return memory ? new (memory) T() : nullptr;
    }
    void* Alloc(size_t n)
    {
        log.push_back("new:" + std::to_string(n));
        return failAlloc ? nullptr : heap;
    }
    void* AllocWords(size_t n, size_t)
    {
        log.push_back("words:" + std::to_string(n));
        return heap;
    }
    void Free(void* p) { log.push_back("free:" + Name(p)); }
    void DeallocateWords(void* p, uint32_t bytes) { log.push_back("dealloc:" + Name(p) + "," + std::to_string(bytes)); }
    void DeallocateSmall(void* p, uint32_t bytes) { log.push_back("small:" + Name(p) + "," + std::to_string(bytes)); }
    void DeleteWidget(GuiWidget& w) { log.push_back("delete:" + Id(w)); }
    void SetVisible(GuiWidget& w, uint32_t v) { log.push_back("visible:" + Id(w) + "," + std::to_string(v)); }
    void DrawOverlay(GuiWidget& w) { log.push_back("overlay:" + Id(w)); }
    void SetItemCount(GuiScrollBarData&, uint32_t n) { log.push_back("count:" + std::to_string(n)); }
    GuiScrollBarData* ConstructScroll(
            void* mem, int x0, int y0, int x1, int y1, uint32_t top, uint32_t count, uint32_t visible, uint32_t id)
    {
        log.push_back("scroll:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1) +
                "," + std::to_string(top) + "," + std::to_string(count) + "," + std::to_string(visible) + "," + std::to_string(id));
        (void)mem;
        madeBar.GuiWidget::m_id = 201;
        return &madeBar;
    }
    void SetFrame(GuiWidget& w, int frame) { log.push_back("frame:" + Id(w) + "," + std::to_string(frame)); }
    void FillSelection(const int32_t* r)
    {
        log.push_back(
                "fill:" + std::to_string(r[0]) + "," + std::to_string(r[1]) + "," + std::to_string(r[2]) + "," + std::to_string(r[3]));
    }
};

std::string Join(const std::vector<std::string>& v)
{
    std::string out;
    for (const auto& s : v) out += s + " ";
    return out;
}

constexpr int kMaxCells = 6;
struct Fixture {
    FakeEnv env;
    ListBox list{};
    GuiScrollBarData bar{};
    obl::ui::GuiListRowCell cells[kMaxCells]{};
    obl::ui::GuiListRowCell* vec[kMaxCells]{};
    GuiWidget widgets[kMaxCells]{};
    int32_t selection[6]{10, 11, 12, 13, 14, 15};

    explicit Fixture(int count, bool withBar = true)
    {
        list.m_id = 100;
        bar.GuiWidget::m_id = 200; // GuiScrollBarData has a field of its own named m_id
        list.m_rowHeight = 16;
        list.m_visibleRows = 3;
        list.m_slotCount = 3;
        list.m_rowCount = 10;
        list.m_enabled = 1;
        list.m_y0 = 50;
        list.m_selBegin = selection;
        list.m_selEnd = selection + 3;
        list.m_cellsBegin = vec;
        list.m_cellsEnd = vec + count;
        for (int i = 0; i < count; ++i) {
            widgets[i].m_id = i;
            cells[i].m_widget = &widgets[i];
            cells[i].m_row = i;
            cells[i].m_value = 1000 + i;
            vec[i] = &cells[i];
        }
        if (withBar) list.m_scroll = &bar;
    }
};

} // namespace

OBL_TEST_CASE("gui_lists: ClearSelection moves only the end, NextSelectedRow advances the cursor and reads the entry")
{
    Fixture f(0);
    f.list.m_selCursor = 1;
    OBL_CHECK_EQ(NextSelectedRow(f.list), 12);
    OBL_CHECK_EQ(f.list.m_selCursor, 2);
    ClearSelection(f.list);
    OBL_CHECK(f.list.m_selEnd == f.list.m_selBegin);
    OBL_CHECK_EQ(f.selection[0], 10);
}

OBL_TEST_CASE("gui_lists: SetScrollTop clamps below zero, tells the bar and moves the cells of the shown rows")
{
    Fixture f(3);
    f.list.m_scrollTop = 7;
    f.cells[0].m_row = 1;
    f.cells[1].m_row = 2;
    f.cells[2].m_row = 9;
    SetScrollTop(f.list, 1, f.env);
    OBL_CHECK_EQ(f.list.m_scrollTop, 1);
    // slots 0..2 show rows 1..3: cell 0 (row 1) at slot 0, cell 1 (row 2) at slot 1; cell 2 (row 9) is hidden. x comes from the bounds.
    const std::vector<std::string> expected = {"top:1", "bounds:0", "pos:0:0,50", "bounds:1", "pos:1:10,66"};
    OBL_CHECK_EQ(Join(f.env.log), Join(expected));
    f.env.log.clear();
    SetScrollTop(f.list, -5, f.env);
    OBL_CHECK_EQ(f.list.m_scrollTop, 0);
    OBL_CHECK_EQ(f.env.log.front(), "top:0");
}

OBL_TEST_CASE("gui_lists: SetScrollTop without a bar only lays out; a row height product wraps")
{
    Fixture f(1, false);
    f.list.m_rowHeight = std::numeric_limits<int32_t>::max();
    f.list.m_slotCount = 2;
    f.cells[0].m_row = 4;
    SetScrollTop(f.list, 3, f.env);
    OBL_CHECK(f.env.log.size() == 2);
    OBL_CHECK_EQ(f.env.log[0], "bounds:0");
    OBL_CHECK_EQ(f.env.log[1], "pos:0:0,-2147483599"); // (INT_MAX * 1 + 50) wraps
}

OBL_TEST_CASE("gui_lists: EnsureRowVisible scrolls up to a row above the window and down to one below it")
{
    Fixture f(0);
    f.list.m_scrollTop = 5;
    EnsureRowVisible(f.list, 2, f.env);
    OBL_CHECK_EQ(f.list.m_scrollTop, 2);
    OBL_CHECK_EQ(f.env.log.front(), "top:2");
    f.env.log.clear();
    // rows 2..4 are shown; row 6 needs top = 6 - 3 + 1 = 4
    EnsureRowVisible(f.list, 6, f.env);
    OBL_CHECK_EQ(f.list.m_scrollTop, 4);
    f.env.log.clear();
    EnsureRowVisible(f.list, 5, f.env); // already inside 4..6
    OBL_CHECK(f.env.log.empty());
    // top + slots beyond the row count falls back to rows - visible rows
    f.list.m_rowCount = 5;
    f.list.m_visibleRows = 3;
    EnsureRowVisible(f.list, 8, f.env);
    OBL_CHECK_EQ(f.list.m_scrollTop, 2);
}

OBL_TEST_CASE("gui_lists: EnsureRowVisible treats a negative row as far below the window")
{
    Fixture f(0, false);
    f.list.m_scrollTop = 2;
    f.list.m_rowCount = 4;
    f.list.m_visibleRows = 5;
    EnsureRowVisible(f.list, static_cast<uint32_t>(-1), f.env);
    // not above the window (unsigned), below it: top = row - slots + 1 = 0xfffffffd; 3 + top wraps to 0, so the rows fallback is not taken
    // and the "negative" top clamps to 0
    OBL_CHECK_EQ(f.list.m_scrollTop, 0);
}

OBL_TEST_CASE("gui_lists: Update runs the bar first, follows its row, updates every cell and ticks the press timer")
{
    Fixture f(2);
    f.bar.m_top = 4;
    f.list.m_scrollTop = 0;
    f.list.m_holding = 1;
    f.list.m_holdTime = 0.5f;
    f.cells[0].m_row = 4;
    f.cells[1].m_row = 6;
    const float dt = 0.25f;
    uint32_t bits;
    std::memcpy(&bits, &dt, 4);
    Update(f.list, bits, f.env);
    OBL_CHECK_EQ(f.list.m_scrollTop, 4);
    OBL_CHECK_EQ(f.list.m_holdTime, 0.75f);
    // rows 4..6 sit in slots 0..2: cell 0 (row 4) at slot 0, cell 1 (row 6) at slot 2
    const std::string dtText = std::to_string(bits);
    const std::vector<std::string> expected = {
            "update:200:" + dtText, "bounds:0", "pos:0:0,50", "bounds:1", "pos:1:10,82", "update:0:" + dtText, "update:1:" + dtText};
    OBL_CHECK_EQ(Join(f.env.log), Join(expected));
}

OBL_TEST_CASE("gui_lists: Update with an unchanged top row does not lay out; a released press keeps its timer")
{
    Fixture f(1);
    f.bar.m_top = 0;
    f.list.m_holdTime = 0.5f;
    Update(f.list, 0x3f800000, f.env);
    OBL_CHECK_EQ(f.list.m_holdTime, 0.5f);
    OBL_CHECK_EQ(f.env.log.size(), 2u);
}

OBL_TEST_CASE("gui_lists: Draw, Invalidate and Translate forward in order")
{
    Fixture f(2);
    Draw(f.list, f.env);
    Invalidate(f.list, f.env);
    f.list.m_barX0 = 1;
    f.list.m_barY0 = 2;
    f.list.m_barX1 = 3;
    f.list.m_barY1 = 4;
    Translate(f.list, 10, 20, f.env);
    const std::vector<std::string> expected = {"draw:200", "base-invalidate:100", "invalidate:0", "invalidate:1",
            "base-translate:100:10,20", "translate:200:10,20", "translate:0:10,20", "translate:1:10,20"};
    OBL_CHECK_EQ(Join(f.env.log), Join(expected));
    OBL_CHECK_EQ(f.list.m_barX0, 11);
    OBL_CHECK_EQ(f.list.m_barY0, 22);
    OBL_CHECK_EQ(f.list.m_barX1, 13);
    OBL_CHECK_EQ(f.list.m_barY1, 24);
}

OBL_TEST_CASE("gui_lists: without a bar Draw and Translate skip it")
{
    Fixture f(0, false);
    Draw(f.list, f.env);
    OBL_CHECK(f.env.log.empty());
    Translate(f.list, 1, 1, f.env);
    OBL_CHECK_EQ(f.env.log.size(), 1u);
}

OBL_TEST_CASE("gui_lists: OnMouseLeft gives the bar and the active row buttons the click first")
{
    Fixture f(3);
    f.env.answerId = 1;
    f.cells[0].m_active = 1;
    f.cells[1].m_active = 1;
    f.cells[2].m_active = 1;
    OBL_CHECK_EQ(OnMouseLeft(f.list, 5, 6, 1, f.env), 5u); // the raw byte
    const std::vector<std::string> expected = {"left:200:1", "left:0:1", "left:1:1"};
    OBL_CHECK_EQ(Join(f.env.log), Join(expected));
    OBL_CHECK_EQ(f.list.m_repeat, 0);
    f.env.log.clear();
    f.env.answerId = 200; // the bar answers: nothing else is asked
    OBL_CHECK_EQ(OnMouseLeft(f.list, 5, 6, 1, f.env), 5u);
    OBL_CHECK_EQ(f.env.log.size(), 1u);
}

OBL_TEST_CASE("gui_lists: OnMouseLeft skips inactive buttons and non-button cells, and a disabled list answers false")
{
    Fixture f(2, false);
    f.cells[0].m_active = 0;
    f.widgets[1].m_type = 10;
    f.cells[1].m_active = 1;
    f.list.m_enabled = 0;
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 0, 1, f.env), 0u);
    OBL_CHECK(f.env.log.empty());
}

OBL_TEST_CASE("gui_lists: a press inside selects the row under the cursor and arms the repeat timer")
{
    Fixture f(0, false);
    f.env.selfHit = true;
    f.env.rowTop = 40;
    f.list.m_scrollTop = 2;
    f.list.m_rowHeight = 16;
    f.list.m_holdTime = 9.0f;
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 40 + 16 * 3 + 5, 1, f.env), 1u);
    const std::vector<std::string> expected = {"hit:100", "bounds:100", "key:29", "key:157", "select:5,1"};
    OBL_CHECK_EQ(Join(f.env.log), Join(expected));
    OBL_CHECK_EQ(f.list.m_holding, 1);
    OBL_CHECK_EQ(f.list.m_holdTime, 0.0f);
}

OBL_TEST_CASE("gui_lists: a second press within the delay sets the repeat flag, a later one keeps the timer running")
{
    Fixture f(0, false);
    f.env.selfHit = true;
    f.list.m_holding = 1;
    f.list.m_holdTime = 0.1f;
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 50, 1, f.env), 1u);
    OBL_CHECK_EQ(f.list.m_repeat, 1);
    OBL_CHECK_EQ(f.list.m_holding, 0);
    f.list.m_holding = 1;
    f.list.m_holdTime = 0.25f;
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 50, 1, f.env), 1u);
    OBL_CHECK_EQ(f.list.m_repeat, 0);
    OBL_CHECK_EQ(f.list.m_holding, 1);
    f.list.m_holdTime = std::numeric_limits<float>::quiet_NaN(); // a NaN also takes the carry branch
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 50, 1, f.env), 1u);
    OBL_CHECK_EQ(f.list.m_repeat, 1);
}

OBL_TEST_CASE("gui_lists: Ctrl toggles the row (argument 0), skips the second key query once the first is down and arms nothing")
{
    Fixture f(0, false);
    f.env.selfHit = true;
    f.env.ctrlLeft = true;
    f.list.m_holding = 1;
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 50, 1, f.env), 1u);
    OBL_CHECK(f.env.log.size() == 4);
    OBL_CHECK_EQ(f.env.log[3], "select:3,0");
    OBL_CHECK_EQ(f.list.m_holding, 0);
    f.env.log.clear();
    f.env.ctrlLeft = false;
    f.env.ctrlRight = true;
    f.list.m_scrollTop = 0;
    OnMouseLeft(f.list, 0, 50, 1, f.env);
    OBL_CHECK_EQ(f.env.log[2], "key:29");
    OBL_CHECK_EQ(f.env.log[3], "key:157");
    OBL_CHECK_EQ(f.env.log[4], "select:3,0");
}

OBL_TEST_CASE("gui_lists: a miss or another button state releases the repeat and answers false")
{
    Fixture f(0, false);
    f.list.m_holding = 1;
    f.env.selfHit = false;
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 0, 1, f.env), 0u);
    OBL_CHECK_EQ(f.list.m_holding, 0);
    f.list.m_holding = 1;
    f.env.selfHit = true;
    f.env.log.clear();
    OBL_CHECK_EQ(OnMouseLeft(f.list, 0, 0, 2, f.env), 0u);
    OBL_CHECK_EQ(f.list.m_holding, 0);
    OBL_CHECK(f.env.log.empty()); // state 2 never asks for the hit
}

OBL_TEST_CASE("gui_lists: OnMouseWheel needs a bar and the cursor over the list or the bar")
{
    Fixture f(1, false);
    OBL_CHECK_EQ(OnMouseWheel(f.list, 1, 2, 3, f.env), 0u);
    OBL_CHECK(f.env.log.empty());
    Fixture g(1);
    OBL_CHECK_EQ(OnMouseWheel(g.list, 1, 2, 3, g.env), 0u);
    OBL_CHECK(g.env.log == (std::vector<std::string>{"hit:100", "hit:200"}));
    g.env.log.clear();
    g.env.barHit = true;
    OBL_CHECK_EQ(OnMouseWheel(g.list, 1, 2, -1, g.env), 0x81u);
    OBL_CHECK(g.env.log == (std::vector<std::string>{"hit:100", "hit:200", "wheel:200:-1"}));
    g.env.log.clear();
    g.env.selfHit = true;
    OnMouseWheel(g.list, 1, 2, 4, g.env);
    OBL_CHECK(g.env.log == (std::vector<std::string>{"hit:100", "wheel:200:4"}));
}

OBL_TEST_CASE("gui_lists: PollRowButton returns the value of the first clicked button in the row and consumes the click")
{
    Fixture f(3, false);
    for (auto& cell : f.cells) cell.m_row = 2;
    f.widgets[1].m_type = 10; // not a button
    f.widgets[0].m_clicked = 1;
    f.widgets[2].m_clicked = 1;
    f.env.answerId = 2;
    int32_t value = -1;
    OBL_CHECK_EQ(PollRowButton(f.list, 2, &value, f.env), 1u);
    OBL_CHECK_EQ(value, 1002);
    OBL_CHECK_EQ(f.widgets[2].m_clicked, 0);
    OBL_CHECK_EQ(f.widgets[0].m_clicked, 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"clicked:0", "clicked:2"}));
    value = -1;
    OBL_CHECK_EQ(PollRowButton(f.list, 7, &value, f.env), 0u);
    OBL_CHECK_EQ(value, -1);
}

OBL_TEST_CASE("gui_lists: Construct fills the fields, copies the name and leaves the row count and the press timer alone")
{
    Fixture f(0, false);
    ListBox list{1, 2, 3, 4};
    list.m_id = 77;
    list.m_rowCount = 123;
    list.m_holdTime = 4.5f;
    list.m_selCursor = 9;
    list.m_scroll = &f.bar;
    const int32_t bar[4] = {5, 6, 7, 8};
    ListBox* result = Construct(list, bar, "ticker", 16, 5, f.env);
    OBL_CHECK(result == &list);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"new:7"}));
    OBL_CHECK_EQ(list.m_barX0, 5);
    OBL_CHECK_EQ(list.m_barY0, 6);
    OBL_CHECK_EQ(list.m_barX1, 7);
    OBL_CHECK_EQ(list.m_barY1, 8);
    OBL_CHECK_EQ(list.m_rowHeight, 16);
    OBL_CHECK_EQ(list.m_visibleRows, 5);
    OBL_CHECK_EQ(list.m_enabled, 1);
    OBL_CHECK_EQ(list.m_multiSelect, 0);
    OBL_CHECK_EQ(list.m_type, kListBoxType);
    OBL_CHECK(list.m_scroll == nullptr);
    OBL_CHECK(list.m_cellsBegin == nullptr && list.m_cellsEnd == nullptr && list.m_cellsCap == nullptr);
    OBL_CHECK(list.m_selBegin == nullptr && list.m_selEnd == nullptr && list.m_selCap == nullptr);
    OBL_CHECK_EQ(list.m_selCursor, 0);
    OBL_CHECK_EQ(std::string(list.m_name), "ticker");
    OBL_CHECK_EQ(list.m_id, 77);
    OBL_CHECK_EQ(list.m_rowCount, 123u);
    OBL_CHECK_EQ(list.m_holdTime, 4.5f);
}

OBL_TEST_CASE("gui_lists: Destruct frees the name, the cells, the bar and both vectors in a fixed order")
{
    Fixture f(2);
    f.env.label = {{f.list.m_name, "name"}, {&f.cells[0], "c0"}, {&f.cells[1], "c1"}, {f.selection, "sel"}, {f.vec, "vec"}};
    char name[] = "x";
    f.list.m_name = name;
    f.env.label[name] = "name";
    f.list.m_selCap = f.selection + 6;
    f.list.m_cellsCap = f.vec + 4;
    f.list.m_slotCount = 3;
    f.list.m_scrollTop = 4;
    f.list.m_rowCount = 9;
    Destruct(f.list, f.env);
    const std::vector<std::string> expected = {"free:name", "delete:0", "free:c0", "delete:1", "free:c1", "delete:200", "dealloc:sel,24",
            "small:vec," + std::to_string(4 * sizeof(void*))};
    OBL_CHECK_EQ(Join(f.env.log), Join(expected));
    OBL_CHECK(f.list.m_cellsEnd == f.list.m_cellsBegin);
    OBL_CHECK(f.list.m_selEnd == f.list.m_selBegin);
    OBL_CHECK_EQ(f.list.m_slotCount, 0u);
    OBL_CHECK_EQ(f.list.m_scrollTop, 0);
    OBL_CHECK_EQ(f.list.m_rowCount, 0u);
    OBL_CHECK(f.list.m_scroll == &f.bar);
}

OBL_TEST_CASE("gui_lists: Destruct skips empty storage and returns a large cell vector to operator_delete")
{
    Fixture f(0, false);
    obl::ui::GuiListRowCell* storage[33]{};
    auto capAt = [&](size_t cells) { return storage + cells; };
    f.list.m_selBegin = f.list.m_selEnd = f.list.m_selCap = nullptr;
    f.list.m_cellsBegin = storage;
    f.list.m_cellsEnd = storage;
    f.list.m_cellsCap = capAt(33);
    f.env.label = {{storage, "vec"}};
    Destruct(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"free:?", "free:vec"}));
    f.env.log.clear();
    f.list.m_cellsCap = capAt(32);
    Destruct(f.list, f.env);
    OBL_CHECK_EQ(f.env.log[1], "small:vec," + std::to_string(32 * sizeof(void*)));
    f.env.log.clear();
    f.list.m_cellsBegin = f.list.m_cellsEnd = f.list.m_cellsCap = nullptr;
    Destruct(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"free:?"}));
}

OBL_TEST_CASE("gui_lists: ClearRows keeps the buttons (deactivated), deletes the other cells, drops the bar and invalidates the list")
{
    Fixture f(4);
    f.widgets[0].m_type = 0;
    f.widgets[1].m_type = 3;
    f.widgets[2].m_type = 10;
    f.widgets[3].m_type = 0;
    f.cells[0].m_active = 1;
    f.cells[3].m_active = 1;
    for (int i = 0; i < 4; ++i) f.env.label[&f.cells[i]] = "c" + std::to_string(i);
    f.list.m_slotCount = 3;
    f.list.m_scrollTop = 2;
    f.list.m_rowCount = 8;
    ClearRows(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"delete:1", "free:c1", "delete:2", "free:c2", "delete:200", "invalidate:100"}));
    OBL_CHECK(f.list.m_cellsEnd - f.list.m_cellsBegin == 2);
    OBL_CHECK(f.vec[0] == &f.cells[0] && f.vec[1] == &f.cells[3]);
    OBL_CHECK_EQ(f.cells[0].m_active, 0);
    OBL_CHECK_EQ(f.cells[3].m_active, 0);
    OBL_CHECK(f.list.m_scroll == nullptr);
    OBL_CHECK_EQ(f.list.m_slotCount, 0u);
    OBL_CHECK_EQ(f.list.m_scrollTop, 0);
    OBL_CHECK_EQ(f.list.m_rowCount, 0u);
}

OBL_TEST_CASE("gui_lists: ClearRows on an empty list only resets the counters and invalidates")
{
    Fixture f(0, false);
    f.list.m_rowCount = 3;
    f.list.m_selCap = f.selection + 6;
    ClearRows(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"invalidate:100"}));
    OBL_CHECK_EQ(f.list.m_rowCount, 0u);
}

OBL_TEST_CASE("gui_lists: SetVisible passes the value on, hiding deletes every cell and resets the counters but keeps the bar")
{
    Fixture f(2);
    f.widgets[0].m_type = 0;
    for (int i = 0; i < 2; ++i) f.env.label[&f.cells[i]] = "c" + std::to_string(i);
    f.list.m_slotCount = 3;
    f.list.m_scrollTop = 2;
    f.list.m_rowCount = 8;
    SetVisible(f.list, 0x101, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"base-visible:100,257", "visible:200,257"}));
    OBL_CHECK_EQ(f.list.m_rowCount, 8u);
    f.env.log.clear();
    SetVisible(f.list, 0x100, f.env); // the low byte is zero: hides
    OBL_CHECK_EQ(Join(f.env.log), Join({"base-visible:100,256", "visible:200,256", "delete:0", "free:c0", "delete:1", "free:c1"}));
    OBL_CHECK(f.list.m_cellsEnd == f.list.m_cellsBegin);
    OBL_CHECK(f.list.m_scroll == &f.bar);
    OBL_CHECK_EQ(f.list.m_slotCount, 0u);
    OBL_CHECK_EQ(f.list.m_scrollTop, 0);
    OBL_CHECK_EQ(f.list.m_rowCount, 0u);
}

OBL_TEST_CASE("gui_lists: UpdateScrollBar creates the bar over the bar rectangle, updates its count, or deletes it")
{
    Fixture f(0, false);
    f.list.m_visibleRows = 4;
    f.list.m_rowCount = 4;
    f.list.m_scrollTop = 2;
    f.list.m_barX0 = 10;
    f.list.m_barY0 = 20;
    f.list.m_barX1 = 30;
    f.list.m_barY1 = 40;
    UpdateScrollBar(f.list, f.env);
    OBL_CHECK(f.env.log.empty() && f.list.m_scroll == nullptr); // fits: no bar
    f.list.m_rowCount = 5;
    UpdateScrollBar(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"new:" + std::to_string(sizeof(obl::ui::GuiScrollBarData)), "scroll:10,20,30,40,2,5,4,3"}));
    OBL_CHECK(f.list.m_scroll == &f.env.madeBar);
    f.env.log.clear();
    f.list.m_rowCount = 9;
    UpdateScrollBar(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"count:9"}));
    f.env.log.clear();
    f.list.m_rowCount = 2;
    UpdateScrollBar(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"delete:201"}));
    OBL_CHECK(f.list.m_scroll == nullptr);
    f.env.log.clear();
    f.list.m_rowCount = 6;
    f.env.failAlloc = true;
    UpdateScrollBar(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"new:" + std::to_string(sizeof(obl::ui::GuiScrollBarData))}));
    OBL_CHECK(f.list.m_scroll == nullptr);
    f.list.m_visibleRows = -1; // compared unsigned: 6 is not above 0xffffffff
    f.env.log.clear();
    UpdateScrollBar(f.list, f.env);
    OBL_CHECK(f.env.log.empty());
}

OBL_TEST_CASE("gui_lists: DrawOverlay paints selected rows green, then draws each row's cells by type")
{
    Fixture f(5);
    f.list.m_scrollTop = 1;
    f.list.m_slotCount = 3;
    f.list.m_rowHeight = 16;
    f.list.m_x0 = 4;
    f.list.m_x1 = 99;
    f.list.m_y0 = 50;
    f.env.rowTop = 60;
    f.selection[0] = 2;
    f.selection[1] = 9;
    f.list.m_selEnd = f.selection + 2;
    f.widgets[0].m_type = 0;
    f.cells[0].m_row = 1;
    f.cells[0].m_active = 1;
    f.widgets[1].m_type = 0;
    f.cells[1].m_row = 1;
    f.cells[1].m_active = 0;
    f.widgets[2].m_type = 10;
    f.cells[2].m_row = 2;
    f.widgets[3].m_type = 3;
    f.cells[3].m_row = 2;
    f.widgets[4].m_type = 10;
    f.cells[4].m_row = 3;
    DrawOverlay(f.list, f.env);
    const std::vector<std::string> expected = {"overlay:200", "invalidate:0", "draw:0", "bounds:100", "fill:4,76,99,92", "frame:2,1",
            "draw:2", "overlay:3", "frame:4,0", "draw:4"};
    OBL_CHECK_EQ(Join(f.env.log), Join(expected));
}

OBL_TEST_CASE("gui_lists: DrawOverlay does nothing for an empty or wrapping window; a reversed selection finds nothing")
{
    Fixture f(1, false);
    f.list.m_slotCount = 0;
    DrawOverlay(f.list, f.env);
    OBL_CHECK(f.env.log.empty());
    f.list.m_scrollTop = -1;
    f.list.m_slotCount = 3; // -1 + 3 wraps below the start
    f.cells[0].m_row = 0;
    DrawOverlay(f.list, f.env);
    OBL_CHECK(f.env.log.empty());
    f.list.m_scrollTop = 0;
    f.list.m_slotCount = 1;
    f.list.m_selEnd = f.list.m_selBegin - 1; // reversed range
    f.widgets[0].m_type = 3;
    DrawOverlay(f.list, f.env);
    OBL_CHECK_EQ(Join(f.env.log), Join({"overlay:0"}));
}

OBL_TEST_CASE("gui_lists: selection clamps signed rows, preserves singleton and honours only replace low byte")
{
    Fixture f(1, false);
    f.list.m_rowCount = 3;
    f.list.m_selEnd = f.list.m_selBegin;
    SelectRow(f.list, 999, 0, f.env);
    OBL_CHECK_EQ(f.list.m_selBegin[0], 2);
    f.list.m_multiSelect = 1;
    SelectRow(f.list, 2, 0, f.env);
    OBL_CHECK_EQ(f.list.m_selEnd - f.list.m_selBegin, 1);
    SelectRow(f.list, uint32_t(-1), 0x100, f.env);
    OBL_CHECK_EQ(f.list.m_selEnd - f.list.m_selBegin, 2);
    OBL_CHECK_EQ(f.list.m_selBegin[0], -1);
    SelectRow(f.list, 2, 0, f.env);
    OBL_CHECK_EQ(f.list.m_selEnd - f.list.m_selBegin, 1);
    OBL_CHECK_EQ(f.list.m_selBegin[0], -1);
    f.list.m_cellsEnd = f.list.m_cellsBegin;
    SelectRow(f.list, 0, 1, f.env);
    OBL_CHECK_EQ(f.list.m_selBegin[0], -1);
}

OBL_TEST_CASE("gui_lists: word vector growth keeps capacity and pool free threshold")
{
    FakeEnv env;
    int32_t values[33];
    for (int i = 0; i < 33; ++i) values[i] = i;
    for (int count : {0, 1, 16, 32, 33}) {
        int32_t* begin = count ? values : nullptr;
        int32_t* end = count ? values + count : nullptr;
        int32_t* cap = end;
        env.log.clear();
        AppendWord(begin, end, cap, -17, env);
        OBL_CHECK_EQ(end - begin, count + 1);
        OBL_CHECK_EQ(cap - begin, count ? count * 2 : 1);
        OBL_CHECK_EQ(begin[count], -17);
        if (count) OBL_CHECK_EQ(begin[count - 1], count - 1);
        if (count == 32) OBL_CHECK_EQ(env.log.back(), "small:?,128");
        if (count == 33) OBL_CHECK_EQ(env.log.back(), "free:?");
    }
}

OBL_TEST_CASE("gui_lists: selection sorts long signed ranges and removes only the first duplicate")
{
    FakeEnv env;
    ListBox list{};
    Cell* cells[1]{};
    int32_t rows[80]{};
    for (int i = 0; i < 70; ++i) rows[i] = 35 - i;
    list.m_cellsBegin = cells;
    list.m_cellsEnd = cells + 1;
    list.m_selBegin = rows;
    list.m_selEnd = rows + 70;
    list.m_selCap = rows + 80;
    list.m_selCursor = 19;
    list.m_rowCount = 100;
    list.m_multiSelect = 1;
    SelectRow(list, uint32_t(-50), 0, env);
    OBL_CHECK_EQ(list.m_selEnd - rows, 71);
    OBL_CHECK_EQ(rows[0], -50);
    OBL_CHECK(std::is_sorted(rows, list.m_selEnd));
    OBL_CHECK_EQ(list.m_selCursor, 19);
    rows[0] = 3;
    rows[1] = 3;
    rows[2] = 1;
    list.m_selEnd = rows + 3;
    SelectRow(list, 3, 0, env);
    OBL_CHECK_EQ(list.m_selEnd - rows, 2);
    OBL_CHECK_EQ(rows[0], 1);
    OBL_CHECK_EQ(rows[1], 3);
}

OBL_TEST_CASE("gui_lists: pointer growth keeps pool boundaries and storage sizes")
{
    struct GrowthEnv : FakeEnv {
        size_t defaultBytes = 0, policyBytes = 0;
        void* AllocWords(size_t bytes, size_t requestedBytes)
        {
            defaultBytes = bytes;
            policyBytes = requestedBytes;
            return FakeEnv::AllocWords(bytes, requestedBytes);
        }
    };
    for (size_t count : {16u, 32u, 33u}) {
        GrowthEnv env;
        void* slots[33]{};
        int value = 7;
        slots[0] = &value;
        void** begin = slots;
        void** end = slots + count;
        void** cap = end;
        env.label[slots] = "old";
        AppendWord(begin, end, cap, static_cast<void*>(&value), env);
        OBL_CHECK_EQ(env.defaultBytes, 2 * count * sizeof(void*));
        OBL_CHECK_EQ(env.policyBytes, 2 * count * 4);
        OBL_CHECK_EQ(env.log.back(), count <= 32 ? "small:old," + std::to_string(count * sizeof(void*)) : "free:old");
        OBL_CHECK_EQ(end - begin, static_cast<ptrdiff_t>(count + 1));
        OBL_CHECK(begin[0] == &value && begin[count] == &value);
    }
}
