#include <cstdint>
// GuiTab / GuiTabWindow (src/game/ui/gui_tabs.h): page walks, the tab button sprite, mouse routing and tab switching.

#include "unit_test.h"
#include "gui_fake_records.h"
#include "leak_ok.h"

#include "game/ui/gui_tabs.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace obl::gui_tabs;
using obl::gui_canvas::RecordingCanvas;
using obl::gui_canvas::SpriteMetrics;
using obl::ui::GuiDefineData;
using GuiTab = obl::ui::GuiTabData;
using GuiTabWindow = obl::ui::GuiTabWindowData;

namespace {

// Widgets are plain objects told apart by `m_id`; the environment answers for them and logs every virtual and base call in order.
struct FakeEnv {
    std::vector<std::string> log;
    int allocated = 0, freed = 0, tabNodes = 0, tabNodesFreed = 0;
    int answerFrom = -1; // widget id from which OnMouseLeft / OnMouseWheel answer true (-1 = never)
    uint8_t selfHit = 0, hovered = 0;
    int sprites = 0;

    static std::string Id(const obl::ui::GuiWidget& w) { return std::to_string(w.m_id); }
    uint8_t Answer(const obl::ui::GuiWidget& w) const { return answerFrom >= 0 && w.m_id >= answerFrom; }

    void* AllocateNode()
    {
        ++allocated;
        return std::calloc(1, obl::gui_containers::kNodeBytes);
    }
    void FreeNode(void* p)
    {
        ++freed;
        log.push_back("free");
        std::free(p);
    }
    void* CopyString(const char* s)
    {
        void* p = std::malloc(std::strlen(s) + 1);
        std::strcpy(static_cast<char*>(p), s);
        return p;
    }
    void* NewTabNode()
    {
        ++tabNodes;
        return LeakOk(std::calloc(1, kTabNodeBytes));
    }
    void Free(void* p)
    {
        ++tabNodesFreed;
        log.push_back("delete");
        std::free(p);
    }
    obl::ui::SpriteInstance* CreateSprite(const char* name)
    {
        log.push_back(std::string("sprite:") + name);
        ++sprites;
        return reinterpret_cast<obl::ui::SpriteInstance*>(0x1000);
    }
    void ReleaseSprite(obl::ui::SpriteInstance*)
    {
        log.push_back("release-sprite");
        --sprites;
    }

    void BaseConstruct(obl::ui::GuiWidget& w, int x0, int y0, int x1, int y1)
    {
        obl::gui_widgets::Construct(w, x0, y0, x1, y1);
        log.push_back("base-construct");
    }
    void BaseDestruct(obl::ui::GuiWidget&) { log.push_back("base-destruct"); }
    void BaseSetOrigin(obl::ui::GuiWidget& w, int dx, int dy)
    {
        obl::gui_widgets::SetOrigin(w, dx, dy);
        log.push_back("base-origin");
    }
    void BaseTranslate(obl::ui::GuiWidget& w, int dx, int dy)
    {
        obl::gui_widgets::Translate(w, dx, dy);
        log.push_back("base-translate");
    }
    void BaseSetVisible(obl::ui::GuiWidget& w, uint32_t v)
    {
        w.m_visible = static_cast<uint8_t>(v);
        log.push_back("base-visible:" + std::to_string(v));
    }
    void BaseInvalidate(obl::ui::GuiWidget& w)
    {
        w.m_dirty = 1;
        log.push_back("base-invalidate");
    }
    void TrackTooltip(obl::ui::GuiWidget&, int, int, int state) { log.push_back("tooltip:" + std::to_string(state)); }

    uint8_t HitTest(obl::ui::GuiWidget& w, int x, int y)
    {
        log.push_back("hit:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y));
        return selfHit;
    }
    uint8_t IsHovered(obl::ui::GuiWidget& w)
    {
        log.push_back("hovered:" + Id(w));
        return hovered;
    }
    uint8_t OnMouseLeft(obl::ui::GuiWidget& w, int, int, int state)
    {
        log.push_back("left:" + Id(w) + ":" + std::to_string(state));
        return Answer(w);
    }
    uint8_t OnMouseWheel(obl::ui::GuiWidget& w, int, int, int delta)
    {
        log.push_back("wheel:" + Id(w) + ":" + std::to_string(delta));
        return Answer(w);
    }
    void RefreshSelection(obl::ui::GuiWidget& w) { log.push_back("selection:" + Id(w)); }
    void SetOrigin(obl::ui::GuiWidget& w, int dx, int dy)
    {
        log.push_back("origin:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void Translate(obl::ui::GuiWidget& w, int dx, int dy)
    {
        log.push_back("translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void Refresh(obl::ui::GuiWidget& w) { log.push_back("refresh:" + Id(w)); }
    void SetVisible(obl::ui::GuiWidget& w, uint32_t v) { log.push_back("visible:" + Id(w) + ":" + std::to_string(v)); }
    void Draw(obl::ui::GuiWidget& w) { log.push_back("draw:" + Id(w)); }
    void DrawOverlay(obl::ui::GuiWidget& w) { log.push_back("overlay:" + Id(w)); }
    void Invalidate(obl::ui::GuiWidget& w) { log.push_back("invalidate:" + Id(w)); }
    void Update(obl::ui::GuiWidget& w, uint32_t dt) { log.push_back("update:" + Id(w) + ":" + std::to_string(dt)); }

    // The tab window asks a tab directly (not through the vtable), so the fake runs the real routine on the same environment.
    uint8_t TabMouse(GuiTab& tab, int x, int y, int state, bool active)
    {
        return ::obl::gui_tabs::TabMouse(tab, x, y, state, active, *this);
    }
    void InvalidateAllDialogs() { log.push_back("invalidate-dialogs"); }
    void PlayClick() { log.push_back("click"); }
};

// A tab with pages of the given types (ids 10, 11, ...), built by the same AddChild the game uses.
struct TabFixture {
    FakeEnv env;
    GuiTab tab{1, 2, 3, 4};
    std::vector<obl::ui::GuiWidget> pages;
    explicit TabFixture(std::vector<int> types) : pages(types.size())
    {
        TabConstruct(tab, "UI3_tab", env);
        tab.m_id = 100;
        for (size_t i = 0; i < types.size(); ++i) {
            pages[i] = {};
            pages[i].m_id = 10 + static_cast<int>(i);
            pages[i].m_type = types[i];
            obl::gui_containers::AddChild(tab.m_children, &pages[i], env);
        }
        env.log.clear();
    }
    ~TabFixture()
    {
        env.log.clear();
        TabDestruct(tab, env);
    }
};

// A window of `count` tabs (plain widgets: the window never looks inside them except for the mouse).
struct WindowFixture {
    FakeEnv env;
    GuiTabWindow window{0, 0, 500, 300};
    std::vector<GuiTab> tabs;
    explicit WindowFixture(int count) : tabs(count)
    {
        TabWindowConstruct(window);
        for (int i = 0; i < count; ++i) {
            tabs[i] = {};
            tabs[i].m_id = i;
            tabs[i].m_children = nullptr;
            TabWindowAddChild(window, &tabs[i], env);
        }
        env.log.clear();
    }
    ~WindowFixture()
    {
        env.log.clear();
        TabWindowDestruct(window, env);
    }
};

} // namespace

OBL_TEST_CASE("gui_tabs: a tab is built with type 9, the button sprite and an empty list; destruction releases both")
{
    FakeEnv env;
    GuiTab tab{5, 6, 7, 8};
    TabConstruct(tab, "UI3_st_t_info", env);
    OBL_CHECK_EQ(tab.m_x0, 5);
    OBL_CHECK_EQ(tab.m_baseY1, 8);
    OBL_CHECK_EQ(tab.m_type, 9);
    OBL_CHECK_EQ(tab.m_state, 0);
    OBL_CHECK(tab.m_sprite != nullptr);
    OBL_CHECK(tab.m_children->m_next == tab.m_children);
    OBL_CHECK_EQ(tab.m_drawnState, 0);
    OBL_CHECK(env.log == (std::vector<std::string>{"sprite:UI3_st_t_info"}));
    env.log.clear();
    TabDestruct(tab, env);
    OBL_CHECK(env.log == (std::vector<std::string>{"release-sprite", "free"}));
    OBL_CHECK_EQ(env.sprites, 0);
}

OBL_TEST_CASE("gui_tabs: SetVisible stores the state from the low byte only and forwards the whole word")
{
    TabFixture f({5, 5});
    TabSetVisible(f.tab, 0x12345601u, f.env);
    OBL_CHECK_EQ(f.tab.m_state, 1);
    OBL_CHECK_EQ(f.tab.m_visible, 1);
    TabSetVisible(f.tab, 0x12345600u, f.env);
    OBL_CHECK_EQ(f.tab.m_state, 0);
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{"base-visible:305419777", "visible:10:305419777", "visible:11:305419777", "base-visible:305419776",
                    "visible:10:305419776", "visible:11:305419776"}));
}

OBL_TEST_CASE("gui_tabs: Update and Refresh reach the pages only while the tab is shown and visible")
{
    TabFixture f({5, 5});
    TabUpdate(f.tab, 7, f.env);
    TabRefresh(f.tab, f.env);
    OBL_CHECK(f.env.log.empty()); // not shown
    f.tab.m_state = 1;
    TabUpdate(f.tab, 7, f.env);
    OBL_CHECK(f.env.log.empty()); // shown but not visible
    f.tab.m_visible = 1;
    TabUpdate(f.tab, 7, f.env);
    TabRefresh(f.tab, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"update:10:7", "update:11:7", "refresh:10", "refresh:11"}));
}

OBL_TEST_CASE("gui_tabs: Invalidate forces a repaint of the sprite and only passes through a shown page")
{
    TabFixture f({5});
    f.tab.m_drawnState = 0;
    f.tab.m_dirty = 0;
    TabInvalidate(f.tab, f.env);
    OBL_CHECK_EQ(f.tab.m_drawnState, kNeverDrawn);
    OBL_CHECK_EQ(f.tab.m_dirty, 0); // the widget's own flag is left alone
    OBL_CHECK(f.env.log.empty());
    f.tab.m_state = 1;
    TabInvalidate(f.tab, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"invalidate:10"}));
}

OBL_TEST_CASE("gui_tabs: origin and translation go to the base first, then every page")
{
    TabFixture f({5, 5});
    TabSetOrigin(f.tab, 3, 4, f.env);
    TabTranslate(f.tab, -1, -2, f.env);
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{
                    "base-origin", "origin:10:3,4", "origin:11:3,4", "base-translate", "translate:10:-1,-2", "translate:11:-1,-2"}));
    OBL_CHECK_EQ(f.tab.m_x0, 1 + 3 - 1);
}

OBL_TEST_CASE("gui_tabs: the wheel stops at the first page that takes it, shown or not")
{
    TabFixture f({5, 5, 5});
    f.env.answerFrom = 11;
    OBL_CHECK_EQ(TabOnMouseWheel(f.tab, 1, 2, -120, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"wheel:10:-120", "wheel:11:-120"}));
    TabFixture empty({});
    OBL_CHECK_EQ(TabOnMouseWheel(empty.tab, 1, 2, 3, empty.env), 0);
}

OBL_TEST_CASE("gui_tabs: a shown tab draws its pages with the lists last, a hidden one draws only the button")
{
    TabFixture f({obl::gui_tabs::kListType, 5, kListType, 6});
    RecordingCanvas canvas([](const char*) { return SpriteMetrics{2, 20, 20}; });
    const auto sprite = canvas.SeedSprite("UI3_tab", {2, 20, 20}, 3, 0, 0);
    f.tab.m_state = 0;
    f.tab.m_drawnState = 0;
    TabDraw(f.tab, canvas, sprite, f.env);
    OBL_CHECK(canvas.calls.empty());
    OBL_CHECK(f.env.log.empty());

    f.tab.m_state = 1;
    f.tab.m_x0 = 30;
    f.tab.m_y0 = 40;
    TabDraw(f.tab, canvas, sprite, f.env);
    OBL_REQUIRE(canvas.calls.size() == 1u);
    OBL_CHECK_EQ(canvas.calls[0].direction, 1);
    OBL_CHECK_EQ(canvas.calls[0].x, 30);
    OBL_CHECK_EQ(canvas.calls[0].y, 40);
    OBL_CHECK_EQ(canvas.calls[0].depth, 0u);
    OBL_CHECK_EQ(f.tab.m_drawnState, 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"draw:11", "draw:13", "draw:10", "draw:12"}));

    // Nothing changed: the sprite is not painted again but the pages are.
    f.env.log.clear();
    TabDraw(f.tab, canvas, sprite, f.env);
    OBL_CHECK_EQ(canvas.calls.size(), 1u);
    OBL_CHECK_EQ(f.env.log.size(), 4u);
}

OBL_TEST_CASE("gui_tabs: a state past the artwork's directions is clamped to the last one")
{
    TabFixture f({});
    RecordingCanvas canvas;
    const auto sprite = canvas.SeedSprite("UI3_tab", {2, 20, 20}, 3, 0, 0);
    f.tab.m_state = 3;
    f.tab.m_drawnState = 0;
    TabDraw(f.tab, canvas, sprite, f.env);
    OBL_REQUIRE(canvas.calls.size() == 1u);
    OBL_CHECK_EQ(canvas.calls[0].direction, 1);
    OBL_CHECK_EQ(f.tab.m_drawnState, 3);
}

OBL_TEST_CASE("gui_tabs: the overlay pass orders pages like Draw and needs a shown tab")
{
    TabFixture f({kListType, 5});
    TabDrawOverlay(f.tab, f.env);
    OBL_CHECK(f.env.log.empty());
    f.tab.m_state = 1;
    TabDrawOverlay(f.tab, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"overlay:11", "overlay:10"}));
}

OBL_TEST_CASE("gui_tabs: an inactive tab answers only for a click on itself; state 3 clears the hover flag")
{
    TabFixture f({5, 5});
    f.env.selfHit = 1;
    f.env.hovered = 1;
    OBL_CHECK_EQ(TabMouse(f.tab, 5, 6, 1, false, f.env), 1);
    OBL_CHECK_EQ(f.tab.m_hovered, 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"tooltip:1", "hit:100:5,6", "hovered:100"}));
    f.env.log.clear();
    OBL_CHECK_EQ(TabMouse(f.tab, 5, 6, 2, false, f.env), 0); // a held button is not a click
    OBL_CHECK_EQ(TabMouse(f.tab, 5, 6, 3, false, f.env), 0); // blocked by a modal dialog
    OBL_CHECK_EQ(f.tab.m_hovered, 0);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"tooltip:2", "hit:100:5,6", "hovered:100", "tooltip:3", "hovered:100"}));
}

OBL_TEST_CASE("gui_tabs: an active tab gives the mouse to its pages first and refreshes their selection on an unclaimed click")
{
    TabFixture f({5, 5});
    f.env.selfHit = 1;
    f.env.hovered = 1;
    f.env.answerFrom = 11;
    OBL_CHECK_EQ(TabMouse(f.tab, 1, 1, 1, true, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"tooltip:1", "hit:100:1,1", "left:10:1", "left:11:1"}));

    f.env.log.clear();
    f.env.answerFrom = -1;
    OBL_CHECK_EQ(TabMouse(f.tab, 1, 1, 1, true, f.env), 0);
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{
                    "tooltip:1", "hit:100:1,1", "left:10:1", "left:11:1", "hovered:100", "selection:10", "selection:11"}));

    f.env.log.clear();
    OBL_CHECK_EQ(TabMouse(f.tab, 1, 1, 2, true, f.env), 0); // only a click refreshes
    OBL_CHECK(f.env.log == (std::vector<std::string>{"tooltip:2", "hit:100:1,1", "left:10:2", "left:11:2", "hovered:100"}));
}

OBL_TEST_CASE("gui_tabs: the first tab added is current; a Define stands for its first child")
{
    FakeEnv env;
    GuiTabWindow window{1, 2, 3, 4};
    TabWindowConstruct(window);
    OBL_CHECK_EQ(window.m_type, 8);
    OBL_CHECK(window.m_tabs == nullptr);
    OBL_CHECK(window.m_current == nullptr);

    obl::ui::GuiWidget a{}, b{}, c{};
    a.m_id = 1;
    b.m_id = 2;
    c.m_id = 3;
    TabWindowAddChild(window, &a, env);
    OBL_CHECK(window.m_current == &a);
    TabWindowAddChild(window, &b, env);
    OBL_CHECK(window.m_current == &a);
    OBL_CHECK(window.m_tabs->m_next->m_widget == &b);
    OBL_CHECK(window.m_tabs->m_next->m_next == nullptr);

    GuiDefineData define{};
    obl::gui_containers::DefineConstruct(define, "T", env);
    obl::gui_containers::AddChild(define.m_children, &c, env);
    TabWindowAddChild(window, &define, env);
    OBL_CHECK(window.m_tabs->m_next->m_next->m_widget == &c);
    obl::gui_containers::DefineDestruct(define, env);

    env.log.clear();
    env.tabNodesFreed = 0;
    TabWindowDestruct(window, env);
    OBL_CHECK(env.log == (std::vector<std::string>{"delete", "delete", "delete"}));
    OBL_CHECK_EQ(env.tabNodes, 3);
    OBL_CHECK_EQ(env.tabNodesFreed, 3);
    OBL_CHECK_EQ(a.m_id, 1); // the tabs are not destroyed with the window
}

OBL_TEST_CASE("gui_tabs: window methods forward to every tab in list order")
{
    WindowFixture f(3);
    TabWindowUpdate(f.window, 9, f.env);
    TabWindowRefresh(f.window, f.env);
    TabWindowDraw(f.window, f.env);
    TabWindowDrawOverlay(f.window, f.env);
    TabWindowTranslate(f.window, 1, 2, f.env);
    TabWindowSetOrigin(f.window, 3, 4, f.env);
    TabWindowInvalidate(f.window, f.env);
    const std::vector<std::string> expected = {"update:0:9", "update:1:9", "update:2:9", "refresh:0", "refresh:1", "refresh:2", "draw:0",
            "draw:1", "draw:2", "overlay:0", "overlay:1", "overlay:2", "base-translate", "translate:0:1,2", "translate:1:1,2",
            "translate:2:1,2", "base-origin", "origin:0:3,4", "origin:1:3,4", "origin:2:3,4", "base-invalidate", "invalidate:0",
            "invalidate:1", "invalidate:2"};
    OBL_CHECK(f.env.log == expected);
}

OBL_TEST_CASE("gui_tabs: showing the window shows only the current tab, hiding hides them all")
{
    WindowFixture f(2);
    TabWindowSetVisible(f.window, 1, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"base-visible:1", "visible:0:0", "visible:1:0", "visible:0:1"}));
    f.env.log.clear();
    TabWindowSetVisible(f.window, 0, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"base-visible:0", "visible:0:0", "visible:1:0"}));
}

OBL_TEST_CASE("gui_tabs: the wheel goes only to the current tab")
{
    WindowFixture f(2);
    f.env.answerFrom = 0;
    OBL_CHECK_EQ(TabWindowOnMouseWheel(f.window, 1, 1, 5, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"wheel:0:5"}));
    WindowFixture empty(0);
    OBL_CHECK_EQ(TabWindowOnMouseWheel(empty.window, 1, 1, 5, empty.env), 0);
}

OBL_TEST_CASE("gui_tabs: SelectTab switches without a sound; an index past the end or an empty window does nothing")
{
    WindowFixture f(3);
    TabWindowSelectTab(f.window, 2, f.env);
    OBL_CHECK(f.window.m_current == &f.tabs[2]);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"visible:0:0", "visible:2:1", "invalidate-dialogs"}));
    f.env.log.clear();
    TabWindowSelectTab(f.window, 3, f.env);
    OBL_CHECK(f.window.m_current == &f.tabs[2]);
    OBL_CHECK(f.env.log.empty());
    TabWindowSelectTab(f.window, -5, f.env); // a negative index picks the first tab
    OBL_CHECK(f.window.m_current == &f.tabs[0]);
    WindowFixture empty(0);
    TabWindowSelectTab(empty.window, 0, empty.env);
    OBL_CHECK(empty.env.log.empty());
}

OBL_TEST_CASE("gui_tabs: tab switching on a click only happens for the state 1 on a hovered inactive tab")
{
    // Three tabs with empty page lists so the active tab's walk is a no-op.
    FakeEnv env;
    GuiTabWindow window{0, 0, 500, 300};
    TabWindowConstruct(window);
    GuiTab tabs[3]{};
    obl::gui_containers::Node sentinels[3]{};
    for (int i = 0; i < 3; ++i) {
        tabs[i].m_id = i;
        sentinels[i].m_next = sentinels[i].m_prev = &sentinels[i];
        tabs[i].m_children = &sentinels[i];
        TabWindowAddChild(window, &tabs[i], env);
    }
    env.log.clear();
    env.selfHit = 1;
    env.hovered = 1;
    OBL_CHECK_EQ(TabWindowOnMouseLeft(window, 4, 4, 1, env), 1);
    OBL_CHECK(window.m_current == &tabs[1]);
    OBL_CHECK(env.log ==
            (std::vector<std::string>{"tooltip:1", "hit:0:4,4", "hovered:0", "tooltip:1", "hit:1:4,4", "hovered:1", "visible:0:0",
                    "visible:1:1", "invalidate-dialogs", "click"}));
}

OBL_TEST_CASE("gui_tabs: a held button or an unhovered tab does not switch; the active tab's answer wins")
{
    FakeEnv env;
    GuiTabWindow window{0, 0, 500, 300};
    TabWindowConstruct(window);
    GuiTab tabs[2]{};
    obl::gui_containers::Node sentinels[2]{};
    obl::ui::GuiWidget page{};
    obl::gui_containers::Node pageNode{};
    for (int i = 0; i < 2; ++i) {
        tabs[i].m_id = i;
        sentinels[i].m_next = sentinels[i].m_prev = &sentinels[i];
        tabs[i].m_children = &sentinels[i];
        TabWindowAddChild(window, &tabs[i], env);
    }
    env.hovered = 1;
    env.log.clear();
    OBL_CHECK_EQ(TabWindowOnMouseLeft(window, 4, 4, 2, env), 0);
    OBL_CHECK(window.m_current == &tabs[0]);
    env.hovered = 0;
    env.log.clear();
    OBL_CHECK_EQ(TabWindowOnMouseLeft(window, 4, 4, 1, env), 0);
    OBL_CHECK(window.m_current == &tabs[0]);

    // A page of the current tab that takes the click ends the pass before any other tab is asked.
    page.m_id = 50;
    pageNode = {&sentinels[0], &sentinels[0], &page};
    sentinels[0].m_next = sentinels[0].m_prev = &pageNode;
    env.answerFrom = 50;
    env.log.clear();
    OBL_CHECK_EQ(TabWindowOnMouseLeft(window, 4, 4, 1, env), 1);
    OBL_CHECK(env.log == (std::vector<std::string>{"tooltip:1", "hit:0:4,4", "left:50:1"}));
}
