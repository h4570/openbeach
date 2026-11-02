// The shared GUI canvas (src/game/ui/gui_canvas.h): what RecordingCanvas notes, and how Compare reports a difference.

#include "core/char16.h"
#include "unit_test.h"

#include "game/ui/gui_canvas.h"

#include <string>

using namespace obl::gui_canvas;

OBL_TEST_CASE("gui_canvas: fills preserve packed colour and distinguish XYWH before clipping")
{
    RecordingCanvas canvas;
    canvas.FillRect(-1, 2, 3, 4, 0x1234);
    canvas.FillRectBySize(-1, 2, 3, 4, 0x1234);
    OBL_REQUIRE(canvas.calls.size() == 2);
    OBL_CHECK(canvas.calls[0].kind == Call::Fill);
    OBL_CHECK_EQ(canvas.calls[0].left, -1);
    OBL_CHECK_EQ(canvas.calls[0].color, 0x1234);
    OBL_CHECK(!Compare({canvas.calls[0]}, {canvas.calls[1]}).empty());
    auto wrong = canvas.calls[0];
    wrong.color ^= 1;
    OBL_CHECK(!Compare({canvas.calls[0]}, {wrong}).empty());
    OBL_CHECK(Describe(canvas.calls[1]).find("fill-xywh") != std::string::npos);
}

namespace {

SpriteMetrics Metrics(const char* name)
{
    return std::string(name) == "wide" ? SpriteMetrics{3, 40, 12} : SpriteMetrics{1, 8, 8};
}

} // namespace

OBL_TEST_CASE("gui_canvas: a sprite draw reports the instance state at the draw, not at the setters")
{
    RecordingCanvas canvas(Metrics);
    const SpriteId id = canvas.CreateSprite("wide", CreateCall::Bank);
    OBL_CHECK(canvas.Metrics(id) == (SpriteMetrics{3, 40, 12}));
    canvas.SetAnimType(id, 3);
    canvas.SetFrame(id, 2);
    canvas.SetDirection(id, 1);
    canvas.DrawClipped(id, 10, 20, 0xffff);
    canvas.SetFrame(id, 5);
    canvas.Draw(id, 1, 2, 3, 4, 5, 6, 7);
    canvas.ReleaseSprite(id);

    OBL_REQUIRE(canvas.calls.size() == 3);
    OBL_CHECK(canvas.calls[0].kind == Call::Create);
    OBL_CHECK(canvas.calls[0].create == CreateCall::Bank);
    OBL_CHECK(canvas.calls[0].name == "wide");
    OBL_CHECK(canvas.calls[1].kind == Call::DrawClipped);
    OBL_CHECK(canvas.calls[1].frame == 2);
    OBL_CHECK(canvas.calls[1].animType == 3);
    OBL_CHECK(canvas.calls[1].direction == 1);
    OBL_CHECK(canvas.calls[1].depth == 0xffffu);
    OBL_CHECK(canvas.calls[1].time == kClippedTime);
    OBL_CHECK(canvas.calls[2].kind == Call::Draw);
    OBL_CHECK(canvas.calls[2].frame == 5);
    OBL_CHECK(canvas.calls[2].left == 3);
    OBL_CHECK(canvas.calls[2].bottom == 6);
    OBL_CHECK(canvas.calls[2].name == "wide");
    OBL_CHECK_EQ(canvas.created, 1);
    OBL_CHECK_EQ(canvas.released, 1);
    OBL_CHECK_EQ(canvas.Live(), 0);
}

OBL_TEST_CASE("gui_canvas: several instances live at once, a double release counts once")
{
    RecordingCanvas canvas(Metrics);
    const SpriteId a = canvas.CreateSprite("a", CreateCall::Sprite);
    const SpriteId b = canvas.CreateSprite("b", CreateCall::Sprite);
    OBL_CHECK(a != b);
    canvas.SetFrame(a, 1);
    canvas.SetFrame(b, 9);
    canvas.DrawClipped(b, 0, 0, 0);
    canvas.DrawClipped(a, 0, 0, 0);
    OBL_CHECK_EQ(canvas.calls[2].frame, 9);
    OBL_CHECK_EQ(canvas.calls[3].frame, 1);
    OBL_CHECK_EQ(canvas.Live(), 2);
    canvas.ReleaseSprite(a);
    canvas.ReleaseSprite(a);
    OBL_CHECK_EQ(canvas.Live(), 1);
    // A handle that was never handed out does nothing instead of crashing.
    canvas.SetFrame(kNoSprite, 1);
    canvas.DrawClipped(kNoSprite, 0, 0, 0);
    canvas.Draw(42, 0, 0, 0, 0, 0, 0, 0);
    OBL_CHECK_EQ(canvas.calls.size(), 4u);
}

OBL_TEST_CASE("gui_canvas: a text draw carries the renderer state the setters left behind")
{
    RecordingCanvas canvas;
    canvas.SeedText(1, TextState{0, 0, 0, 0, 0, false, 2});
    canvas.SetTextRect(1, 5, 6, 70, 8);
    canvas.SetTextColor(1, 0xff8040);
    canvas.SetTextOutlined(1, true);
    canvas.DrawWrapped(1, 0, 0, u"Hello");
    canvas.SetTextAlign(1, 1);
    canvas.DrawLine(1, 3, 4, u"x");
    canvas.DrawWrapped(0, 0, 0, u"other renderer");

    OBL_REQUIRE(canvas.calls.size() == 3);
    const Call& wrapped = canvas.calls[0];
    OBL_CHECK(wrapped.kind == Call::DrawText);
    OBL_CHECK(wrapped.wrapped);
    OBL_CHECK_EQ(wrapped.renderer, 1);
    OBL_CHECK(wrapped.state == (TextState{5, 6, 70, 8, 0xff8040, true, 2}));
    OBL_CHECK(wrapped.text == u"Hello");
    OBL_CHECK(!canvas.calls[1].wrapped);
    OBL_CHECK_EQ(canvas.calls[1].state.align, 1);
    OBL_CHECK_EQ(canvas.calls[1].x, 3);
    OBL_CHECK(canvas.calls[2].state == TextState{});
}

OBL_TEST_CASE("gui_canvas: a renderer outside the table is ignored by setters and recorded by index")
{
    RecordingCanvas canvas;
    canvas.SetTextColor(kTextRenderers, 1);
    canvas.SetTextRect(-1, 1, 1, 1, 1);
    canvas.DrawLine(-1, 0, 0, nullptr);
    OBL_REQUIRE(canvas.calls.size() == 1);
    OBL_CHECK_EQ(canvas.calls[0].renderer, -1);
    OBL_CHECK(canvas.calls[0].text.empty());
    OBL_CHECK(canvas.calls[0].state == TextState{});
}

OBL_TEST_CASE("gui_canvas: Compare finds the first difference and a length mismatch")
{
    RecordingCanvas a, b;
    for (RecordingCanvas* canvas : {&a, &b}) {
        const SpriteId id = canvas->CreateSprite("s", CreateCall::Sprite);
        canvas->SetFrame(id, 1);
        canvas->DrawClipped(id, 1, 2, 3);
        canvas->DrawWrapped(0, 0, 0, u"tą");
    }
    OBL_CHECK(Compare(a.calls, b.calls).empty());

    b.calls[1].frame = 2;
    const std::string frame = Compare(a.calls, b.calls);
    OBL_CHECK(frame.find("call 1 differs") == 0);
    OBL_CHECK(frame.find("frame=1") != std::string::npos);
    OBL_CHECK(frame.find("frame=2") != std::string::npos);
    b.calls[1].frame = 1;

    b.calls[2].state.color = 7;
    OBL_CHECK(Compare(a.calls, b.calls).find("call 2 differs") == 0);
    b.calls[2].state.color = 0;
    b.calls[2].text = u"tĆ";
    const std::string text = Compare(a.calls, b.calls);
    OBL_CHECK(text.find("\\u0105") != std::string::npos); // non-ASCII code units stay readable

    b.calls[2].text = a.calls[2].text;
    b.calls.pop_back();
    const std::string shorter = Compare(a.calls, b.calls);
    OBL_CHECK(shorter.find("expected 3 calls, got 2") == 0);
    OBL_CHECK(shorter.find("missing") != std::string::npos);
    OBL_CHECK(Compare(b.calls, a.calls).find("unexpected") != std::string::npos);
}
