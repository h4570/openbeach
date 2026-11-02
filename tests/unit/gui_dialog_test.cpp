#include <cstdint>
// GuiDialog (src/game/ui/gui_dialog.h): construction, background and child draw order, hit test, slide animation and title-bar dragging.

#include "unit_test.h"
#include "gui_fake_records.h"
#include "leak_ok.h"

#include "game/ui/gui_dialog.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace obl::gui_dialog;
using obl::gui_canvas::Call;
using obl::gui_canvas::RecordingCanvas;
using obl::ui::GuiDialogData;

namespace {

// Widgets are plain objects told apart by `m_id`; the environment answers for them and logs every virtual and base call in order.
struct FakeEnv {
    std::vector<std::string> log;
    int allocated = 0, freed = 0;
    int answerFrom = -1; // child id from which OnMouseLeft / OnMouseWheel answer true (-1 = never)
    uint8_t captionHit = 0, selfHit = 0, hovered = 0, baseHit = 0, spriteHit = 0;
    int screenWidth = 800, screenHeight = 600;
    Size spriteSize{50, 40};
    obl::ui::GuiWidget caption{};

    static std::string Id(const obl::ui::GuiWidget& w) { return std::to_string(w.m_id); }
    uint8_t Answer(const obl::ui::GuiWidget& w) const { return answerFrom >= 0 && w.m_id >= answerFrom; }

    void* AllocateNode()
    {
        ++allocated;
        return LeakOk(std::calloc(1, obl::gui_containers::kNodeBytes));
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
    void Free(void* p)
    {
        log.push_back("delete");
        std::free(p);
    }
    obl::ui::SpriteInstance* CreateSprite(const char* name)
    {
        log.push_back(std::string("sprite:") + name);
        return reinterpret_cast<obl::ui::SpriteInstance*>(0x1000);
    }
    void ReleaseSprite(obl::ui::SpriteInstance*) { log.push_back("release-sprite"); }
    obl::ui::GuiWidget* NewCaption(int x0, int y0, int x1, int y1)
    {
        log.push_back("caption:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1));
        caption.m_id = 50;
        return &caption;
    }
    void DeleteWidget(obl::ui::GuiWidget& w) { log.push_back("delete-widget:" + Id(w)); }

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
    uint8_t BaseHitTest(obl::ui::GuiWidget&, int, int)
    {
        log.push_back("base-hit");
        return baseHit;
    }
    void BaseBounds(obl::ui::GuiWidget& w, int32_t* out)
    {
        out[0] = w.m_x0;
        out[1] = w.m_y0;
        out[2] = w.m_x1;
        out[3] = w.m_y1;
    }

    uint8_t HitTest(obl::ui::GuiWidget& w, int x, int y)
    {
        log.push_back("hit:" + Id(w) + ":" + std::to_string(x) + "," + std::to_string(y));
        return w.m_id == 50 ? captionHit : selfHit;
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
    void SetOrigin(obl::ui::GuiWidget& w, int dx, int dy)
    {
        log.push_back("origin:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void Translate(obl::ui::GuiWidget& w, int dx, int dy)
    {
        log.push_back("translate:" + Id(w) + ":" + std::to_string(dx) + "," + std::to_string(dy));
    }
    void Refresh(obl::ui::GuiWidget& w) { log.push_back("refresh:" + Id(w)); }
    void SelectTab(obl::ui::GuiWidget& w, int index) { log.push_back("tab:" + Id(w) + ":" + std::to_string(index)); }
    void SetVisible(obl::ui::GuiWidget& w, uint32_t v) { log.push_back("visible:" + Id(w) + ":" + std::to_string(v)); }
    void Draw(obl::ui::GuiWidget& w) { log.push_back("draw:" + Id(w)); }
    void DrawOverlay(obl::ui::GuiWidget& w) { log.push_back("overlay:" + Id(w)); }
    void Invalidate(obl::ui::GuiWidget& w) { log.push_back("invalidate:" + Id(w)); }
    void Update(obl::ui::GuiWidget& w, uint32_t dt) { log.push_back("update:" + Id(w) + ":" + std::to_string(dt)); }

    int ScreenWidth() { return screenWidth; }
    int ScreenHeight() { return screenHeight; }
    Size SpriteSize(obl::ui::SpriteInstance*) { return spriteSize; }
    uint8_t SpriteHit(obl::ui::SpriteInstance*, int x, int y)
    {
        log.push_back("sprite-hit:" + std::to_string(x) + "," + std::to_string(y));
        return spriteHit;
    }
    void InvalidateDialog(char* name) { log.push_back(std::string("invalidate-dialog:") + name); }
    void CloseDialog(char* name) { log.push_back(std::string("close:") + name); }
    void RememberPosition(const int32_t* rect, int32_t key)
    {
        log.push_back("position:" + std::to_string(rect[0]) + "," + std::to_string(rect[1]) + ":" + std::to_string(key));
    }
};

uint32_t Bits(float f)
{
    return std::bit_cast<uint32_t>(f);
}

// A dialog at (100, 50)-(400, 350) with children of the given types (ids 10, 11, ...); the title bar answers as id 50.
struct DialogFixture {
    FakeEnv env;
    GuiDialogData dialog{100, 50, 400, 350};
    std::vector<obl::ui::GuiWidget> children;
    explicit DialogFixture(std::vector<int> types = {}, const char* sprite = "NONE") : children(types.size())
    {
        DialogConstruct(dialog, "TestDialog", sprite, 9, 1, env);
        for (size_t i = 0; i < types.size(); ++i) {
            children[i] = {};
            children[i].m_id = 10 + static_cast<int>(i);
            children[i].m_type = types[i];
            DialogAddChild(dialog, &children[i], env);
        }
        env.log.clear();
    }
    ~DialogFixture()
    {
        env.log.clear();
        DialogDestruct(dialog, env);
    }
};

} // namespace

OBL_TEST_CASE(
        "gui_dialog: construction copies the name, makes a title bar over the top 30 rows and picks the background from the sprite name")
{
    FakeEnv env;
    GuiDialogData d{10, 20, 310, 220};
    DialogConstruct(d, "Cleaner", "UI3_back", 77, 0xff, env);
    OBL_CHECK_EQ(d.m_type, 4);
    OBL_CHECK(std::string(d.m_name) == "Cleaner");
    OBL_CHECK(d.m_sprite != nullptr);
    OBL_CHECK_EQ(d.m_clear, 0);
    OBL_CHECK_EQ(d.m_positionKey, 77);
    OBL_CHECK_EQ(d.m_movable, 0xff);
    OBL_CHECK_EQ(d.m_modal, 0);
    OBL_CHECK_EQ(d.m_dragging, 0);
    OBL_CHECK_EQ(d.m_animating, 0);
    OBL_CHECK_EQ(d.m_animType, 0);
    OBL_CHECK_EQ(d.m_minimized, 0);
    OBL_CHECK(d.m_children->m_next == d.m_children);
    OBL_CHECK(d.m_caption == &env.caption);
    OBL_CHECK_EQ(d.m_dragX, 0);
    OBL_CHECK(env.log == (std::vector<std::string>{"sprite:UI3_back", "caption:10,20,310,50"}));

    env.log.clear();
    DialogDestruct(d, env);
    OBL_CHECK(env.log == (std::vector<std::string>{"release-sprite", "delete", "delete-widget:50", "free"}));
}

OBL_TEST_CASE("gui_dialog: CLEAR and NONE (any case) create no sprite; CLEAR is remembered")
{
    FakeEnv env;
    GuiDialogData d;
    DialogConstruct(d, "A", "clear", 0, 0, env);
    OBL_CHECK(d.m_sprite == nullptr);
    OBL_CHECK_EQ(d.m_clear, 1);
    std::free(d.m_name);
    GuiDialogData e;
    DialogConstruct(e, "B", "None", 0, 0, env);
    OBL_CHECK(e.m_sprite == nullptr);
    OBL_CHECK_EQ(e.m_clear, 0);
    std::free(e.m_name);
    OBL_CHECK(obl::ascii::EqualsNoCase("NoNe", "none"));
    OBL_CHECK(!obl::ascii::EqualsNoCase("none", "nonex"));
    OBL_CHECK(!obl::ascii::EqualsNoCase("", "x"));
}

OBL_TEST_CASE("gui_dialog: methods forward to the children in list order; SetOrigin only reaches templates and tab windows and ignores its "
              "arguments")
{
    DialogFixture f({5, 3, 8, 2});
    DialogRefresh(f.dialog, f.env);
    DialogSelectTab(f.dialog, 4, f.env);
    DialogSetOrigin(f.dialog, f.env);
    DialogInvalidate(f.dialog, f.env);
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{"refresh:10", "refresh:11", "refresh:12", "refresh:13", "tab:10:4", "tab:11:4", "tab:12:4",
                    "tab:13:4", "origin:11:100,50", "origin:12:100,50", "base-invalidate", "invalidate:10", "invalidate:11",
                    "invalidate:12", "invalidate:13"}));
    OBL_CHECK_EQ(f.dialog.m_dirty, 1);
}

OBL_TEST_CASE("gui_dialog: SetVisible passes the whole word on and clears the unused word only for a non-zero low byte")
{
    DialogFixture f({5});
    f.dialog.m_minimized = 5;
    DialogSetVisible(f.dialog, 0x100u, f.env);
    OBL_CHECK_EQ(f.dialog.m_minimized, 5);
    DialogSetVisible(f.dialog, 0x101u, f.env);
    OBL_CHECK_EQ(f.dialog.m_minimized, 0);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"base-visible:256", "visible:10:256", "base-visible:257", "visible:10:257"}));
}

OBL_TEST_CASE("gui_dialog: Translate moves the title bar and the children, then reports the new bounds under the position key")
{
    DialogFixture f({5, 5});
    DialogTranslate(f.dialog, 7, -3, f.env);
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{
                    "base-translate", "translate:50:7,-3", "translate:10:7,-3", "translate:11:7,-3", "position:107,47:9"}));
}

OBL_TEST_CASE("gui_dialog: the wheel goes to the first child that takes it")
{
    DialogFixture f({5, 5, 5});
    f.env.answerFrom = 11;
    OBL_CHECK_EQ(DialogOnMouseWheel(f.dialog, 1, 2, 3, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"wheel:10:3", "wheel:11:3"}));
    DialogFixture empty;
    OBL_CHECK_EQ(DialogOnMouseWheel(empty.dialog, 1, 2, 3, empty.env), 0);
}

OBL_TEST_CASE("gui_dialog: Draw paints the background once, then the children with lists last; the overlay has the same order")
{
    DialogFixture f({2, 5, 2, 6}, "UI3_back");
    f.dialog.m_dirty = 1;
    RecordingCanvas canvas;
    const auto sprite = canvas.SeedSprite("UI3_back", {1, 10, 10}, 0, 0, 0);
    DialogDraw(f.dialog, canvas, sprite, f.env);
    OBL_CHECK_EQ(canvas.calls.size(), size_t(1));
    OBL_CHECK(canvas.calls[0].kind == Call::DrawClipped);
    OBL_CHECK_EQ(canvas.calls[0].x, 100);
    OBL_CHECK_EQ(canvas.calls[0].y, 50);
    OBL_CHECK_EQ(canvas.calls[0].depth, 0xfffu);
    OBL_CHECK_EQ(f.dialog.m_dirty, 0);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"draw:11", "draw:13", "draw:10", "draw:12"}));
    f.env.log.clear();
    DialogDraw(f.dialog, canvas, sprite, f.env); // clean: no second background
    OBL_CHECK_EQ(canvas.calls.size(), size_t(1));
    f.env.log.clear();
    DialogDrawOverlay(f.dialog, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"overlay:11", "overlay:13", "overlay:10", "overlay:12"}));
}

OBL_TEST_CASE("gui_dialog: without a sprite the background is white inside the rectangle shrunk by one, unless the script said CLEAR")
{
    DialogFixture f;
    RecordingCanvas canvas;
    f.dialog.m_dirty = 1;
    DialogDraw(f.dialog, canvas, obl::gui_canvas::kNoSprite, f.env);
    OBL_CHECK_EQ(canvas.calls.size(), size_t(1));
    OBL_CHECK(canvas.calls[0].kind == Call::Fill);
    OBL_CHECK_EQ(canvas.calls[0].left, 100);
    OBL_CHECK_EQ(canvas.calls[0].top, 50);
    OBL_CHECK_EQ(canvas.calls[0].right, 399);
    OBL_CHECK_EQ(canvas.calls[0].bottom, 349);
    OBL_CHECK_EQ(canvas.calls[0].color, 0xffff);
    f.dialog.m_clear = 1;
    f.dialog.m_dirty = 1;
    DialogDraw(f.dialog, canvas, obl::gui_canvas::kNoSprite, f.env);
    OBL_CHECK_EQ(canvas.calls.size(), size_t(1));
    OBL_CHECK_EQ(f.dialog.m_dirty, 0);
}

OBL_TEST_CASE("gui_dialog: hit test is the base rectangle without a sprite and the sprite's opaque pixel inside its frame with one")
{
    DialogFixture f;
    f.env.baseHit = 7;
    OBL_CHECK_EQ(DialogHitTest(f.dialog, 1, 2, f.env), 7);
    OBL_CHECK_EQ(f.dialog.m_hovered, 7);

    DialogFixture g({}, "UI3_back");
    g.env.spriteHit = 1;
    OBL_CHECK_EQ(DialogHitTest(g.dialog, 101, 51, g.env), 1); // (1,1) inside the 50x40 frame
    OBL_CHECK_EQ(g.dialog.m_hovered, 1);
    OBL_CHECK_EQ(DialogHitTest(g.dialog, 100, 51, g.env), 0); // the left edge itself is outside (strict)
    OBL_CHECK_EQ(DialogHitTest(g.dialog, 150, 51, g.env), 0); // x == width
    OBL_CHECK_EQ(DialogHitTest(g.dialog, 101, 90, g.env), 0); // y == height
    OBL_CHECK_EQ(g.dialog.m_hovered, 0);
    OBL_CHECK(g.env.log == (std::vector<std::string>{"sprite-hit:1,1"}));
}

OBL_TEST_CASE("gui_dialog: StartAnimation stores its parameters; only type 1 places the window one screen width to the right at once")
{
    DialogFixture f;
    f.dialog.m_baseX0 = 100;
    DialogStartAnimation(f.dialog, 2, Bits(0.5f), 33, f.env);
    OBL_CHECK_EQ(f.dialog.m_animType, 2);
    OBL_CHECK_EQ(f.dialog.m_animating, 1);
    OBL_CHECK_EQ(f.dialog.m_animTarget, 33);
    OBL_CHECK(f.dialog.m_animTime == 0.5f);
    OBL_CHECK(f.dialog.m_animDuration == 0.5f);
    OBL_CHECK(f.env.log.empty());
    DialogStartAnimation(f.dialog, 1, Bits(2.0f), 0, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"translate:0:700,0"}));
}

OBL_TEST_CASE("gui_dialog: a slide-in step moves by the share of the way that the elapsed time covers (truncated), ends at zero time and "
              "updates the children")
{
    DialogFixture f({5});
    f.dialog.m_x0 = 900;
    f.dialog.m_baseX0 = 100;
    DialogStartAnimation(f.dialog, 1, Bits(2.0f), 0, f.env);
    f.env.log.clear();
    DialogUpdate(f.dialog, Bits(0.5f), f.env); // 1.5 left of 2.0: distance -800 * (2.0 - 1.5) / 2.0 = -200
    OBL_CHECK(f.env.log == (std::vector<std::string>{"translate:0:-200,0", "invalidate:0", "update:10:" + std::to_string(Bits(0.5f))}));
    OBL_CHECK(f.dialog.m_animTime == 1.5f);
    OBL_CHECK_EQ(f.dialog.m_animating, 1);
    f.env.log.clear();
    DialogUpdate(f.dialog, Bits(5.0f), f.env); // past the end: the time clamps to zero, the whole distance is covered, the animation stops
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{"translate:0:-800,0", "invalidate:0", "invalidate:0", "update:10:" + std::to_string(Bits(5.0f))}));
    OBL_CHECK_EQ(f.dialog.m_animating, 0);
    OBL_CHECK_EQ(f.dialog.m_animType, 0);
    OBL_CHECK(f.dialog.m_animTime == 0.0f);
}

OBL_TEST_CASE("gui_dialog: a slide-out runs to 100 pixels past the right edge and then closes the dialog by its name")
{
    DialogFixture f;
    f.dialog.m_x0 = 300;
    DialogStartAnimation(f.dialog, 2, Bits(1.0f), 0, f.env);
    f.env.log.clear();
    DialogUpdate(f.dialog, Bits(0.25f), f.env); // (800 - 300 + 100) * 0.25 = 150
    OBL_CHECK(f.env.log == (std::vector<std::string>{"translate:0:150,0", "invalidate:0"}));
    f.dialog.m_x0 = 801; // already past the edge: the timer is cut short and the dialog closes
    f.env.log.clear();
    DialogUpdate(f.dialog, Bits(0.25f), f.env);
    OBL_CHECK(f.env.log ==
            (std::vector<std::string>{
                    "translate:0:49,0", "invalidate:0", "close:TestDialog", "invalidate:0"})); // 99 * 0.5 = 49.5 truncated
    OBL_CHECK_EQ(f.dialog.m_animating, 0);
}

OBL_TEST_CASE("gui_dialog: slide to a target and slide back use their own distances; unrecognized types only tick the timer")
{
    DialogFixture f;
    f.dialog.m_x0 = 400;
    f.dialog.m_baseX0 = 100;
    f.dialog.m_animating = 1;
    f.dialog.m_animDuration = 1.0f;
    f.dialog.m_animTime = 1.0f;
    f.dialog.m_animType = 3;
    f.dialog.m_animTarget = 250;
    DialogUpdate(f.dialog, Bits(0.5f), f.env); // (250 - 400 + 100) * 0.5 = -25
    OBL_CHECK(f.env.log[0] == "translate:0:-25,0");
    f.env.log.clear();
    f.dialog.m_animType = 4;
    f.dialog.m_animTime = 1.0f;
    DialogUpdate(f.dialog, Bits(0.5f), f.env); // (100 - 400) * 0.5 = -150
    OBL_CHECK(f.env.log[0] == "translate:0:-150,0");
    f.env.log.clear();
    f.dialog.m_animType = 9;
    f.dialog.m_animTime = 1.0f;
    DialogUpdate(f.dialog, Bits(0.5f), f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"invalidate:0"}));
}

OBL_TEST_CASE("gui_dialog: a NaN or zero-length animation ends at once without a crash")
{
    DialogFixture f;
    f.dialog.m_animating = 1;
    f.dialog.m_animType = 1;
    f.dialog.m_animDuration = 0.0f;
    f.dialog.m_animTime = 1.0f;
    f.dialog.m_baseX0 = 5;
    DialogUpdate(f.dialog, Bits(0.5f), f.env); // x * 0.0 / 0.0 is NaN: the float-to-int conversion gives 0
    OBL_CHECK(f.env.log[0] == "translate:0:0,0");
    DialogFixture g;
    g.dialog.m_animating = 1;
    g.dialog.m_animType = 0;
    g.dialog.m_animDuration = 1.0f;
    g.dialog.m_animTime = std::numeric_limits<float>::quiet_NaN();
    DialogUpdate(g.dialog, Bits(0.1f), g.env);
    OBL_CHECK_EQ(g.dialog.m_animating, 0); // NaN counts as "ran out"
    OBL_CHECK(g.dialog.m_animTime == 0.0f);
}

OBL_TEST_CASE(
        "gui_dialog: a press goes to the children first, then to the title bar, which starts the drag only for a movable, non-modal dialog")
{
    DialogFixture f({5, 5});
    f.env.captionHit = 1;
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, 120, 60, 1, f.env), 1);
    OBL_CHECK_EQ(f.dialog.m_dragging, 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:120,60", "left:10:1", "left:11:1", "hit:50:120,60"}));

    DialogFixture modal({5});
    modal.env.captionHit = 1;
    modal.dialog.m_modal = 1;
    OBL_CHECK_EQ(DialogOnMouseLeft(modal.dialog, 120, 60, 1, modal.env), 0);
    OBL_CHECK_EQ(modal.dialog.m_dragging, 0);

    DialogFixture child({5, 5});
    child.env.captionHit = 1;
    child.env.answerFrom = 10;
    OBL_CHECK_EQ(DialogOnMouseLeft(child.dialog, 120, 60, 1, child.env), 1);
    OBL_CHECK_EQ(child.dialog.m_dragging, 0); // a child took the click
}

OBL_TEST_CASE("gui_dialog: a click on the dialog itself is swallowed; an animating dialog handles nothing else")
{
    DialogFixture f({5});
    f.env.hovered = 1;
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, 120, 60, 1, f.env), 1); // hovered and pressed
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, 120, 60, 0, f.env), 0); // released: not handled
    f.dialog.m_animating = 1;
    f.env.log.clear();
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, 120, 60, 1, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:120,60", "hovered:0"}));
}

OBL_TEST_CASE("gui_dialog: dragging follows the cursor, keeps 20 pixels of the window on screen on each axis and ends with any other state")
{
    DialogFixture f;
    f.dialog.m_movable = 1;
    f.dialog.m_dragging = 1;
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, 300, 200, 2, f.env), 1); // first step only records the cursor
    OBL_CHECK_EQ(f.dialog.m_dragX, 300);
    OBL_CHECK_EQ(f.dialog.m_dragY, 200);
    f.env.log.clear();
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, 310, 190, 2, f.env), 1);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:310,190", "translate:0:10,-10", "invalidate-dialog:TestDialog"}));
    f.env.log.clear();
    DialogOnMouseLeft(f.dialog, 5000, 190, 2, f.env); // x would put the left edge past the screen: that axis is dropped
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:5000,190"}));
    // The "no cursor" coordinate reuses the previous position and moves nothing.
    f.dialog.m_dragX = 40;
    f.dialog.m_dragY = 50;
    f.env.log.clear();
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, kCursorOutside, 77, 2, f.env), 1);
    OBL_CHECK_EQ(f.dialog.m_dragX, 40);
    OBL_CHECK_EQ(f.dialog.m_dragY, 50);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:-300,77"}));
    // Releasing ends the drag and forgets the cursor.
    OBL_CHECK_EQ(DialogOnMouseLeft(f.dialog, 5, 5, 0, f.env), 0);
    OBL_CHECK_EQ(f.dialog.m_dragging, 0);
    OBL_CHECK_EQ(f.dialog.m_dragX, 0);
    OBL_CHECK_EQ(f.dialog.m_dragY, 0);
}

OBL_TEST_CASE("gui_dialog: the drag clamp tests each edge against 20 pixels of the screen")
{
    DialogFixture f;
    f.dialog.m_dragging = 1;
    f.dialog.m_dragX = 100;
    f.dialog.m_dragY = 100;
    // left edge 100 + 700 = 800 > 780: dropped; right edge 400 - 390 = 10 < 20: dropped (checked separately below)
    f.env.log.clear();
    DialogOnMouseLeft(f.dialog, 800, 100, 2, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:800,100"}));
    f.dialog.m_dragX = 100;
    f.dialog.m_dragY = 100;
    f.env.log.clear();
    DialogOnMouseLeft(f.dialog, -290, 100, 2, f.env);
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:-290,100"}));
    f.dialog.m_dragX = 100;
    f.dialog.m_dragY = 100;
    f.env.log.clear();
    DialogOnMouseLeft(f.dialog, 100, 590, 2, f.env); // top edge 50 + 490 = 540 <= 580: kept
    OBL_CHECK(f.env.log == (std::vector<std::string>{"hit:0:100,590", "translate:0:0,490", "invalidate-dialog:TestDialog"}));
}
