#include <cstdint>
// GuiDefine / GuiMainItem (src/game/ui/gui_containers.h): list walks, mouse routing, construction and the seven backgrounds.

#include "unit_test.h"
#include "gui_fake_records.h"

#include "game/ui/gui_containers.h"
#include "game/ui/gui_widget_virtual.h"

#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace obl::gui_containers;
using obl::gui_canvas::Call;
using obl::gui_canvas::RecordingCanvas;
using obl::gui_canvas::SpriteMetrics;
using obl::ui::GuiDefineData;
using obl::ui::GuiMainItemData;

namespace {

// Children are plain widgets told apart by `m_id`; the environment answers for them and logs every virtual call in order.
struct FakeEnv {
    std::vector<std::string> log;
    std::set<void*> pool; // pool blocks currently handed out
    int allocated = 0, freed = 0;
    int answerFrom = -1; // child id from which the mouse/hit callbacks answer true (-1 = never)
    uint8_t selfHit = 0, hovered = 0, baseHit = 0;

    static std::string Id(const obl::ui::GuiWidget& w) { return std::to_string(w.m_id); }
    uint8_t Answer(const obl::ui::GuiWidget& w) const { return answerFrom >= 0 && w.m_id >= answerFrom; }

    void* AllocateNode()
    {
        ++allocated;
        void* p = std::calloc(1, obl::gui_containers::kNodeBytes);
        pool.insert(p);
        return p;
    }
    void FreeNode(void* p)
    {
        ++freed;
        log.push_back("free");
        pool.erase(p);
        std::free(p);
    }
    void Free(void* p)
    {
        log.push_back("delete");
        std::free(p);
    }
    void* CopyString(const char* s)
    {
        void* p = std::malloc(std::strlen(s) + 1);
        std::strcpy(static_cast<char*>(p), s);
        return p;
    }
    void BaseConstruct(obl::ui::GuiWidget& w, int x0, int y0, int x1, int y1)
    {
        obl::gui_widgets::Construct(w, x0, y0, x1, y1);
        log.push_back("base-construct");
    }
    void BaseDestruct(obl::ui::GuiWidget&) { log.push_back("base-destruct"); }

    uint8_t BaseHitTest(obl::ui::GuiWidget&, int, int)
    {
        log.push_back("base-hit");
        return baseHit;
    }
    uint8_t HitTest(obl::ui::GuiWidget& w, int x, int y)
    {
        log.push_back("hit:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y));
        return &w == widget ? selfHit : Answer(w);
    }
    uint8_t IsHovered(obl::ui::GuiWidget&)
    {
        log.push_back("hovered");
        return hovered;
    }
    uint8_t OnMouseLeft(obl::ui::GuiWidget& w, int x, int y, int state)
    {
        log.push_back("left:" + Id(w) + ":" + std::to_string(state));
        (void)x;
        (void)y;
        return Answer(w);
    }
    uint8_t OnMouseRight(obl::ui::GuiWidget& w, int, int, int state)
    {
        log.push_back("right:" + Id(w) + ":" + std::to_string(state));
        return Answer(w);
    }
    void SetOrigin(obl::ui::GuiWidget& w, int dx, int dy)
    {
        log.push_back("origin:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void Translate(obl::ui::GuiWidget& w, int dx, int dy)
    {
        log.push_back("translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void Refresh(obl::ui::GuiWidget& w) { log.push_back("refresh:" + Id(w)); }
    void SelectTab(obl::ui::GuiWidget& w, int i) { log.push_back("tab:" + Id(w) + ":" + std::to_string(i)); }
    void SetVisible(obl::ui::GuiWidget& w, uint32_t v) { log.push_back("visible:" + Id(w) + ":" + std::to_string(v)); }
    void Draw(obl::ui::GuiWidget& w) { log.push_back("draw:" + Id(w)); }
    void DrawOverlay(obl::ui::GuiWidget& w) { log.push_back("overlay:" + Id(w)); }
    void Invalidate(obl::ui::GuiWidget& w) { log.push_back("invalidate:" + Id(w)); }
    void Update(obl::ui::GuiWidget& w, uint32_t dt) { log.push_back("update:" + Id(w) + ":" + std::to_string(dt)); }

    obl::ui::GuiWidget* widget = nullptr;
};

// A container with `count` children linked through real nodes, built by the same AddChild the game uses.
struct Fixture {
    FakeEnv env;
    GuiMainItemData item{0, 0, 100, 50};
    std::vector<obl::ui::GuiWidget> children;
    explicit Fixture(int count) : children(count)
    {
        MainItemConstruct(item, 0, env);
        item.m_id = -1;
        env.widget = &item;
        for (int i = 0; i < count; ++i) {
            children[i] = {};
            children[i].m_id = i;
            AddChild(item.m_children, &children[i], env);
        }
        env.log.clear();
    }
    ~Fixture()
    {
        env.log.clear();
        MainItemDestruct(item, env);
    }
};

struct RecordingPrimitives final : Primitives {
    std::vector<std::string> log;
    uint32_t Pack(int r, int g, int b) override
    {
        log.push_back("pack " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b));
        return (r << 16) | (g << 8) | b;
    }
    void Line(int x0, int y0, int x1, int y1, uint32_t c) override
    {
        log.push_back("line " + std::to_string(x0) + "," + std::to_string(y0) + " " + std::to_string(x1) + "," + std::to_string(y1) + " " +
                std::to_string(c));
    }
    const char* ScrollBarSliderSprite() override { return "slider"; }
    const char* ScrollBarTrackSprite() override { return "track"; }
    const char* IndexedSprite(int index) override { return index == 0 ? "caption0" : "caption1"; }
};

SpriteMetrics Metrics(const char* name)
{
    if (std::string(name) == "slider") return {1, 10, 6};
    if (std::string(name) == "track") return {1, 10, 5};
    return {1, 7, 9}; // captions
}

std::string FillText(const Call& c)
{
    return "fill " + std::to_string(c.left) + "," + std::to_string(c.top) + "," + std::to_string(c.right) + "," + std::to_string(c.bottom) +
            " " + std::to_string(c.color);
}

} // namespace

OBL_TEST_CASE("gui_containers: construction pins the type, variant and an empty self-linked list")
{
    FakeEnv env;
    GuiMainItemData item{5, 6, 7, 8};
    MainItemConstruct(item, 4, env);
    OBL_CHECK_EQ(item.m_x0, 5);
    OBL_CHECK_EQ(item.m_baseY1, 8);
    OBL_CHECK_EQ(item.m_variant, 4);
    OBL_CHECK_EQ(item.m_index, 0);
    OBL_CHECK_EQ(item.m_type, 0xb);
    OBL_CHECK(item.m_children->m_next == item.m_children);
    OBL_CHECK(item.m_children->m_prev == item.m_children);
    OBL_CHECK_EQ(env.allocated, 1);
    MainItemDestruct(item, env);
    OBL_CHECK_EQ(env.freed, 1);
    OBL_CHECK(env.pool.empty());

    GuiDefineData define;
    DefineConstruct(define, "Window", env);
    OBL_CHECK_EQ(define.m_type, 3);
    OBL_CHECK(std::string(define.m_name) == "Window");
    env.log.clear();
    DefineDestruct(define, env);
    OBL_CHECK(env.log == (std::vector<std::string>{"delete", "free"}));
    OBL_CHECK(env.pool.empty());
}

OBL_TEST_CASE("gui_containers: AddChild appends and destruction returns every node in list order")
{
    Fixture f(3);
    auto* sentinel = f.item.m_children;
    OBL_REQUIRE(sentinel->m_next->m_widget == &f.children[0]);
    OBL_CHECK(sentinel->m_prev->m_widget == &f.children[2]);
    OBL_CHECK(sentinel->m_next->m_next->m_widget == &f.children[1]);
    OBL_CHECK(sentinel->m_prev->m_next == sentinel);
    OBL_CHECK_EQ(f.env.allocated, 4);
    // The destructor of the fixture frees three nodes and the sentinel.
}

OBL_TEST_CASE("gui_containers: forwarding methods reach every child in order with unchanged arguments")
{
    Fixture f(3);
    MainItemUpdate(f.item, 0x3f800000u, f.env);
    MainItemSetVisible(f.item, 0x12345601u, f.env);
    MainItemInvalidate(f.item, f.env);
    f.item.m_dirty = 0;
    MainItemInvalidate(f.item, f.env);
    OBL_CHECK_EQ(f.item.m_dirty, 1);
    MainItemDrawOverlay(f.item, f.env);
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{"update:0:1065353216", "update:1:1065353216", "update:2:1065353216", "visible:0:305419777",
                    "visible:1:305419777", "visible:2:305419777", "invalidate:0", "invalidate:1", "invalidate:2", "invalidate:0",
                    "invalidate:1", "invalidate:2", "overlay:0", "overlay:1", "overlay:2"}));
    OBL_CHECK_EQ(f.item.m_visible, 0); // The main item set visible routine does not store the flag
}

OBL_TEST_CASE("gui_containers: GuiDefine walks the children for every method")
{
    FakeEnv env;
    GuiDefineData define;
    DefineConstruct(define, "T", env);
    obl::ui::GuiWidget a{}, b{};
    a.m_id = 7;
    b.m_id = 8;
    AddChild(define.m_children, &a, env);
    AddChild(define.m_children, &b, env);
    env.log.clear();
    DefineSetOrigin(define, -3, 4, env);
    DefineTranslate(define, 5, -6, env);
    DefineRefresh(define, env);
    DefineSelectTab(define, 2, env);
    DefineSetVisible(define, 1, env);
    DefineDraw(define, env);
    DefineInvalidate(define, env);
    DefineUpdate(define, 9, env);
    OBL_CHECK(env.log ==
            (std::vector<std::string>{"origin:7:-3,4", "origin:8:-3,4", "translate:7:5,-6", "translate:8:5,-6", "refresh:7", "refresh:8",
                    "tab:7:2", "tab:8:2", "visible:7:1", "visible:8:1", "draw:7", "draw:8", "invalidate:7", "invalidate:8", "update:7:9",
                    "update:8:9"}));
    DefineDestruct(define, env);
    OBL_CHECK(env.pool.empty());
}

OBL_TEST_CASE("gui_containers: a class without a table answers Refresh and SelectTab through its virtuals")
{
    struct Probe : obl::ui::GuiWidget {
        int refreshed = 0, tab = -1;
        void Refresh() override { ++refreshed; }
        void SelectTab(int index) override { tab = index; }
    } probe;
    probe.Refresh();
    probe.SelectTab(3);
    OBL_CHECK_EQ(probe.refreshed, 1);
    OBL_CHECK_EQ(probe.tab, 3);
}

OBL_TEST_CASE("gui_containers: hit test asks the children only after the base misses and stops at the first hit")
{
    Fixture f(3);
    f.env.baseHit = 1;
    OBL_CHECK_EQ(MainItemHitTest(f.item, 4, 5, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"base-hit"}));
    f.env.log.clear();
    f.env.baseHit = 0;
    f.env.answerFrom = 1;
    OBL_CHECK_EQ(MainItemHitTest(f.item, 4, 5, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"base-hit", "hit:0:4,5", "hit:1:4,5"}));
    f.env.log.clear();
    f.env.answerFrom = -1;
    OBL_CHECK_EQ(MainItemHitTest(f.item, 4, 5, f.env), 0);
    OBL_CHECK_EQ(f.env.log.size(), 4u);
}

OBL_TEST_CASE("gui_containers: left click goes to the children first, then the hovered panel takes press and hold")
{
    Fixture f(2);
    f.item.m_clicked = 0x80;
    f.item.m_held = 0x80;
    f.item.m_hovered = 0x80;
    f.env.selfHit = 1;
    f.env.hovered = 1;
    OBL_CHECK_EQ(MainItemOnMouseLeft(f.item, 10, 20, 1, f.env), 1);
    OBL_CHECK_EQ(f.item.m_clicked, 1);
    OBL_CHECK_EQ(f.item.m_held, 0);
    OBL_CHECK_EQ(f.item.m_hovered, 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:-1:10,20", "left:0:1", "left:1:1", "hovered"}));

    f.env.log.clear();
    OBL_CHECK_EQ(MainItemOnMouseLeft(f.item, 10, 20, 2, f.env), 1);
    OBL_CHECK_EQ(f.item.m_clicked, 0);
    OBL_CHECK_EQ(f.item.m_held, 1);
    OBL_CHECK_EQ(MainItemOnMouseLeft(f.item, 10, 20, 3, f.env), 0);
    OBL_CHECK_EQ(f.item.m_clicked, 0);
    OBL_CHECK_EQ(f.item.m_held, 0);

    f.env.hovered = 0;
    OBL_CHECK_EQ(MainItemOnMouseLeft(f.item, 10, 20, 1, f.env), 0);
    OBL_CHECK_EQ(f.item.m_clicked, 0);

    // A consuming child ends the walk and the panel keeps its flags clear without asking whether it is hovered.
    f.env.hovered = 1;
    f.env.answerFrom = 0;
    f.env.log.clear();
    OBL_CHECK_EQ(MainItemOnMouseLeft(f.item, 10, 20, 1, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:-1:10,20", "left:0:1"}));
    OBL_CHECK_EQ(f.item.m_clicked, 0);
}

OBL_TEST_CASE("gui_containers: right click returns the first consuming child's answer")
{
    Fixture f(3);
    f.env.answerFrom = 1;
    OBL_CHECK_EQ(MainItemOnMouseRight(f.item, 1, 2, 5, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"right:0:5", "right:1:5"}));
    Fixture empty(0);
    OBL_CHECK_EQ(MainItemOnMouseRight(empty.item, 1, 2, 5, empty.env), 0);
}

OBL_TEST_CASE("gui_containers: the bottom edge follows the top edge and wraps like a 32-bit register")
{
    GuiMainItemData item{};
    item.m_y0 = 10;
    item.m_y1 = 99;
    MainItemSetHeight(item, 25);
    OBL_CHECK_EQ(item.m_y1, 35);
    item.m_y0 = 0x7fffffff;
    MainItemSetHeight(item, 2);
    OBL_CHECK_EQ(item.m_y1, static_cast<int32_t>(0x80000001u));
}

OBL_TEST_CASE("gui_containers: Draw paints the background once per invalidation and always draws the children")
{
    for (int variant : {-1, 0, 8, 100}) {
        Fixture f(1);
        f.item.m_variant = variant;
        f.item.m_dirty = 1;
        RecordingCanvas canvas(Metrics);
        RecordingPrimitives prim;
        MainItemDraw(f.item, canvas, prim, f.env);
        OBL_CHECK(canvas.calls.empty());
        OBL_CHECK(prim.log.empty()); // variants outside 1..7 paint nothing
        OBL_CHECK_EQ(f.item.m_dirty, 0);
    }
    Fixture f(2);
    f.item.m_variant = 6;
    f.item.m_x0 = 10;
    f.item.m_y0 = 20;
    f.item.m_x1 = 90;
    f.item.m_y1 = 60;
    RecordingCanvas canvas(Metrics);
    RecordingPrimitives prim;
    f.item.m_dirty = 1;
    MainItemDraw(f.item, canvas, prim, f.env);
    OBL_CHECK_EQ(prim.log.size(), 6u);
    OBL_CHECK_EQ(f.item.m_dirty, 0);
    MainItemDraw(f.item, canvas, prim, f.env);
    OBL_CHECK_EQ(prim.log.size(), 6u);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"draw:0", "draw:1", "draw:0", "draw:1"}));
}

OBL_TEST_CASE("gui_containers: panel variants 2, 3 and 7 differ only in their edge strips")
{
    const int32_t rect[4] = {10, 20, 90, 60};
    const auto edge = static_cast<uint16_t>((0x7a << 16) | (0x98 << 8) | 0xfa); // the test Pack, truncated like a fill colour
    const auto inner = static_cast<uint16_t>((0xeb << 16) | (0xe9 << 8) | 0xf9);
    auto run = [&](int variant) {
        RecordingCanvas canvas(Metrics);
        RecordingPrimitives prim;
        DrawBackground(variant, rect, 0, canvas, prim);
        std::vector<std::string> out;
        for (const Call& c : canvas.calls) out.push_back(FillText(c));
        return std::pair{out, prim.log};
    };
    const auto two = run(2), three = run(3), seven = run(7);
    OBL_CHECK(two.second == (std::vector<std::string>{"pack 122,152,250", "pack 235,233,249"}));
    OBL_CHECK(two.first ==
            (std::vector<std::string>{FillText({.kind = Call::Fill, .color = edge, .left = 10, .top = 20, .right = 12, .bottom = 60}),
                    FillText({.kind = Call::Fill, .color = edge, .left = 88, .top = 20, .right = 90, .bottom = 60}),
                    FillText({.kind = Call::Fill, .color = edge, .left = 10, .top = 58, .right = 90, .bottom = 60}),
                    FillText({.kind = Call::Fill, .color = inner, .left = 12, .top = 20, .right = 88, .bottom = 58})}));
    OBL_CHECK(three.first[2] == FillText({.kind = Call::Fill, .color = edge, .left = 10, .top = 18, .right = 90, .bottom = 20}));
    OBL_CHECK_EQ(three.first.size(), 4u);
    OBL_CHECK(three.first[3] == two.first[3]);
    OBL_REQUIRE(seven.first.size() == 5);
    OBL_CHECK(seven.first[2] == two.first[2]);
    OBL_CHECK(seven.first[3] == FillText({.kind = Call::Fill, .color = edge, .left = 10, .top = 20, .right = 90, .bottom = 22}));
    OBL_CHECK(seven.first[4] == FillText({.kind = Call::Fill, .color = inner, .left = 12, .top = 22, .right = 88, .bottom = 58}));
}

OBL_TEST_CASE("gui_containers: highlight draws three lines inset one and two pixels")
{
    const int32_t rect[4] = {10, 20, 90, 60};
    RecordingCanvas canvas(Metrics);
    RecordingPrimitives prim;
    DrawBackground(6, rect, 0, canvas, prim);
    OBL_CHECK(canvas.calls.empty());
    OBL_CHECK(prim.log ==
            (std::vector<std::string>{"pack 37,66,189", "line 11,20 88,20 2441917", "pack 147,178,253", "line 11,21 88,21 9679613",
                    "pack 187,208,255", "line 11,22 88,22 12308735"}));
}

OBL_TEST_CASE("gui_containers: scroll bar track and slider use sprite frames 0 and 1 around the shaded columns")
{
    const int32_t rect[4] = {10, 20, 90, 70};
    RecordingCanvas track(Metrics);
    RecordingPrimitives trackPrim;
    DrawBackground(4, rect, 0, track, trackPrim);
    OBL_CHECK_EQ(trackPrim.log.size(), 13u); // six column lines with their packs, plus the pack of the fill
    OBL_CHECK_EQ(track.created, 1);
    OBL_CHECK_EQ(track.released, 1);
    OBL_REQUIRE(track.calls.size() == 4); // the fill comes first, the sprite is created after the columns
    OBL_CHECK(track.calls[0].kind == Call::Fill);
    OBL_CHECK_EQ(track.calls[0].left, 13);
    OBL_CHECK_EQ(track.calls[0].top, 20);
    OBL_CHECK_EQ(track.calls[0].right, 21);
    OBL_CHECK_EQ(track.calls[0].bottom, 69);
    OBL_CHECK(track.calls[1].kind == Call::Create);
    OBL_CHECK(track.calls[1].name == "track");
    OBL_CHECK_EQ(track.calls[2].frame, 0);
    OBL_CHECK_EQ(track.calls[2].x, 10);
    OBL_CHECK_EQ(track.calls[2].y, 20);
    OBL_CHECK_EQ(track.calls[3].frame, 1);
    OBL_CHECK_EQ(track.calls[3].y, 65); // y1 - height of the sprite
    OBL_CHECK_EQ(track.calls[2].animType, 3);

    RecordingCanvas slider(Metrics);
    RecordingPrimitives sliderPrim;
    DrawBackground(5, rect, 0, slider, sliderPrim);
    OBL_REQUIRE(slider.calls.size() == 4);
    OBL_CHECK(slider.calls[0].name == "slider");
    OBL_CHECK_EQ(slider.calls[1].frame, 0);
    OBL_CHECK_EQ(slider.calls[1].y, 20);
    // body = 70 - 2 * 6 - 20 = 38 starting below the 6 pixel cap
    OBL_CHECK(slider.calls[2].kind == Call::Fill);
    OBL_CHECK_EQ(slider.calls[2].top, 26);
    OBL_CHECK_EQ(slider.calls[2].bottom, 64);
    OBL_CHECK_EQ(slider.calls[2].left, 13);
    OBL_CHECK_EQ(slider.calls[2].right, 19);
    OBL_CHECK_EQ(slider.calls[3].frame, 1);
    OBL_CHECK_EQ(slider.calls[3].y, 64);
    OBL_CHECK(sliderPrim.log[1] == "line 10,26 10,64 6858106");

    // Too short for two caps: no body, no lines, the second cap follows the first directly.
    const int32_t shortRect[4] = {10, 20, 40, 30};
    RecordingCanvas squat(Metrics);
    RecordingPrimitives squatPrim;
    DrawBackground(5, shortRect, 0, squat, squatPrim);
    OBL_CHECK(squatPrim.log.empty());
    OBL_REQUIRE(squat.calls.size() == 3);
    OBL_CHECK_EQ(squat.calls[2].y, 26);
}

OBL_TEST_CASE("gui_containers: caption bar starts one sprite width in and ends with the mirrored frame")
{
    const int32_t rect[4] = {10, 20, 90, 60};
    RecordingCanvas canvas(Metrics);
    RecordingPrimitives prim;
    DrawBackground(1, rect, 1, canvas, prim);
    OBL_REQUIRE(canvas.calls.size() == 4);
    OBL_CHECK(canvas.calls[0].name == "caption1");
    OBL_CHECK_EQ(canvas.calls[1].x, 10);
    OBL_CHECK_EQ(canvas.calls[1].frame, 0);
    OBL_CHECK(canvas.calls[2].kind == Call::Fill);
    OBL_CHECK_EQ(canvas.calls[2].left, 17);
    OBL_CHECK_EQ(canvas.calls[2].top, 23);
    OBL_CHECK_EQ(canvas.calls[2].right, 89);
    OBL_CHECK_EQ(canvas.calls[2].bottom, 33);
    OBL_CHECK_EQ(canvas.calls[3].frame, 1);
    OBL_CHECK_EQ(canvas.calls[3].x, 84);
    OBL_CHECK_EQ(canvas.calls[3].y, 20);
    OBL_CHECK_EQ(prim.log.size(), 13u);
    OBL_CHECK(prim.log[1] == "line 17,20 90,20 7170946");
    OBL_CHECK(prim.log[12] == "line 17,35 90,35 15987451");
}
