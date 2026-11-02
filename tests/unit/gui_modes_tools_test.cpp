// The photo mode frame (src/game/ui/gui_modes_tools.h): which sprite set a width picks, where each piece goes, the order of the calls.

#include "core/char16.h"
#include "unit_test.h"

#include "game/ui/gui_modes_tools.h"

#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace obl::gui_modes_tools;
using obl::gui_canvas::Call;
using obl::gui_canvas::RecordingCanvas;
using obl::gui_canvas::SpriteMetrics;

namespace {

// Sizes in the spirit of the game's art: corners differ so a swapped size shows up as a wrong position.
SpriteMetrics MetricsOf(const char* name)
{
    static const std::map<std::string, SpriteMetrics> table = {
            {"ui3_cam_upper_left", {1, 100, 60}},
            {"ui3_cam_upper_right", {1, 90, 70}},
            {"ui3_cam_lowleft", {1, 110, 80}},
            {"ui3_cam_lowright", {1, 120, 50}},
            {"ui3_cam_a_left", {1, 10, 300}},
            {"ui3_cam_a_right", {1, 12, 300}},
            {"ui3_cam_a_top", {1, 400, 14}},
            {"ui3_cam_a_bottom", {1, 400, 16}},
            {"ui3_cam_a_filmstrip", {1, 300, 40}},
            {"ui3_cam_b_left", {1, 20, 400}},
            {"ui3_cam_b_right", {1, 22, 400}},
            {"ui3_cam_b_top", {1, 500, 24}},
            {"ui3_cam_b_bottom", {1, 500, 26}},
            {"ui3_cam_b_filmstrip", {1, 300, 40}},
            {"ui3_cam_c_left", {1, 30, 500}},
            {"ui3_cam_c_right", {1, 32, 500}},
            {"ui3_cam_c_top", {1, 600, 34}},
            {"ui3_cam_c_bottom", {1, 600, 36}},
            {"ui3_cam_c_filmstrip", {1, 300, 40}},
    };
    auto it = table.find(name);
    return it == table.end() ? SpriteMetrics{} : it->second;
}

std::vector<Call> Draws(const RecordingCanvas& canvas)
{
    std::vector<Call> out;
    for (const Call& c : canvas.calls)
        if (c.kind == Call::DrawClipped) out.push_back(c);
    return out;
}

const Call* FindDraw(const std::vector<Call>& draws, const std::string& name)
{
    for (const Call& c : draws)
        if (c.name == name) return &c;
    return nullptr;
}

} // namespace

OBL_TEST_CASE("gui_modes_tools: a width picks the largest frame set that fits it")
{
    OBL_CHECK(SelectCameraFrameSet(320) == CameraFrameSet::None);
    OBL_CHECK(SelectCameraFrameSet(640) == CameraFrameSet::None);
    OBL_CHECK(SelectCameraFrameSet(799) == CameraFrameSet::None);
    OBL_CHECK(SelectCameraFrameSet(800) == CameraFrameSet::A);
    OBL_CHECK(SelectCameraFrameSet(1023) == CameraFrameSet::A);
    OBL_CHECK(SelectCameraFrameSet(1024) == CameraFrameSet::B);
    OBL_CHECK(SelectCameraFrameSet(1279) == CameraFrameSet::B);
    OBL_CHECK(SelectCameraFrameSet(1280) == CameraFrameSet::C);
    OBL_CHECK(SelectCameraFrameSet(1366) == CameraFrameSet::C);
    OBL_CHECK(SelectCameraFrameSet(1600) == CameraFrameSet::C);
    OBL_CHECK(SelectCameraFrameSet(1920) == CameraFrameSet::C);
    OBL_CHECK(SelectCameraFrameSet(3840) == CameraFrameSet::C);
}

OBL_TEST_CASE("gui_modes_tools: the frame at 1024x768 places every piece from the corner sizes and the screen edges")
{
    RecordingCanvas canvas(&MetricsOf);
    DrawCameraFrame(canvas, 1024, 768, 0.04f);
    const std::vector<Call> draws = Draws(canvas);
    OBL_REQUIRE(draws.size() == 9);
    // Draw order: the four corners, then left, right, top, bottom, film strip.
    const char* order[] = {"ui3_cam_upper_left", "ui3_cam_upper_right", "ui3_cam_lowleft", "ui3_cam_lowright", "ui3_cam_b_left",
            "ui3_cam_b_right", "ui3_cam_b_top", "ui3_cam_b_bottom", "ui3_cam_b_filmstrip"};
    for (size_t i = 0; i < 9; ++i) OBL_CHECK_EQ(draws[i].name, std::string(order[i]));
    OBL_CHECK_EQ(draws[0].x, 0);
    OBL_CHECK_EQ(draws[0].y, 0);
    OBL_CHECK_EQ(draws[1].x, 1024 - 90);
    OBL_CHECK_EQ(draws[1].y, 0);
    OBL_CHECK_EQ(draws[2].x, 0);
    OBL_CHECK_EQ(draws[2].y, 768 - 80);
    OBL_CHECK_EQ(draws[3].x, 1024 - 120);
    OBL_CHECK_EQ(draws[3].y, 768 - 50);
    OBL_CHECK_EQ(draws[4].x, 0); // left bar below the upper left corner
    OBL_CHECK_EQ(draws[4].y, 60);
    OBL_CHECK_EQ(draws[5].x, 1024 - 22); // right bar on the right edge, below the upper right corner
    OBL_CHECK_EQ(draws[5].y, 70);
    OBL_CHECK_EQ(draws[6].x, 100); // top bar right of the upper left corner
    OBL_CHECK_EQ(draws[6].y, 0);
    OBL_CHECK_EQ(draws[7].x, 110); // bottom bar right of the lower left corner, on the bottom edge
    OBL_CHECK_EQ(draws[7].y, 768 - 26);
    OBL_CHECK_EQ(draws[8].x, 0x1cf);
    OBL_CHECK_EQ(draws[8].y, 768 - 0x46);
    for (const Call& c : draws) {
        OBL_CHECK_EQ(c.depth, 0xffffu);
        OBL_CHECK(c.time == 0.04f); // the mode's dt, not the widgets' constant
    }
    OBL_CHECK_EQ(canvas.Live(), 0);
}

OBL_TEST_CASE("gui_modes_tools: widths of the game's table use their own set, a wider screen uses the last one")
{
    struct Case {
        int width;
        const char* left;
    };
    for (const Case& c : {Case{800, "ui3_cam_a_left"}, Case{1024, "ui3_cam_b_left"}, Case{1280, "ui3_cam_c_left"},
                 Case{1600, "ui3_cam_c_left"}, Case{1920, "ui3_cam_c_left"}}) {
        RecordingCanvas canvas(&MetricsOf);
        DrawCameraFrame(canvas, c.width, 900, 0.0f);
        const std::vector<Call> draws = Draws(canvas);
        OBL_CHECK_EQ(draws.size(), size_t(9));
        OBL_CHECK(FindDraw(draws, c.left) != nullptr);
        // The right-hand pieces follow the screen's right edge whatever the width.
        const Call* upperRight = FindDraw(draws, "ui3_cam_upper_right");
        OBL_REQUIRE(upperRight != nullptr);
        OBL_CHECK_EQ(upperRight->x, c.width - 90);
        OBL_CHECK_EQ(canvas.Live(), 0);
    }
}

namespace {

// Real-art proportions: at 1280 the top and bottom bars of set C end exactly where the right corners start.
SpriteMetrics RealisticMetricsOf(const std::string& name)
{
    if (name == "ui3_cam_c_top") return {1, 1280 - 100 - 90, 34};
    if (name == "ui3_cam_c_bottom") return {1, 1280 - 110 - 120, 36};
    if (name == "ui3_cam_c_left") return {1, 30, 960 - 60 - 80};
    if (name == "ui3_cam_c_right") return {1, 32, 960 - 70 - 50};
    return MetricsOf(name.c_str());
}

std::vector<Call> BarDraws(const RecordingCanvas& canvas, const std::string& name)
{
    std::vector<Call> out;
    for (const Call& c : canvas.calls)
        if (c.kind == Call::Draw && c.name == name) out.push_back(c);
    return out;
}

} // namespace

OBL_TEST_CASE("gui_modes_tools: at 1280 the bars meet the right corners and nothing is tiled")
{
    RecordingCanvas canvas([](const char* n) { return RealisticMetricsOf(n); });
    DrawCameraFrame(canvas, 1280, 960, 0.0f);
    OBL_CHECK_EQ(BarDraws(canvas, "ui3_cam_c_top").size(), size_t(0));
    OBL_CHECK_EQ(BarDraws(canvas, "ui3_cam_c_bottom").size(), size_t(0));
    OBL_CHECK_EQ(BarDraws(canvas, "ui3_cam_c_left").size(), size_t(0));
    OBL_CHECK_EQ(BarDraws(canvas, "ui3_cam_c_right").size(), size_t(0));
}

OBL_TEST_CASE("gui_modes_tools: a screen wider than 1280 repeats the bar's first columns from its end up to the right corner")
{
    for (int width : {1366, 1600, 1920, 3840}) {
        RecordingCanvas canvas([](const char* n) { return RealisticMetricsOf(n); });
        DrawCameraFrame(canvas, width, 1080, 0.0f);

        struct Bar {
            const char* name;
            int x0; // where the bar sprite ends
            int y;
            int height;
            int rightCorner;
        };
        for (const Bar& bar : {Bar{"ui3_cam_c_top", 100 + 1090, 0, 34, 90}, Bar{"ui3_cam_c_bottom", 110 + 1050, 1080 - 36, 36, 120}}) {
            const std::vector<Call> tiles = BarDraws(canvas, bar.name);
            OBL_REQUIRE(!tiles.empty());
            int x = bar.x0;
            for (const Call& t : tiles) {
                OBL_CHECK_EQ(t.x, x);
                OBL_CHECK_EQ(t.left, 0); // a positive left hits the blitters' quirks (blit-contracts.md)
                OBL_CHECK_EQ(t.y, bar.y);
                OBL_CHECK_EQ(t.top, 0);
                OBL_CHECK_EQ(t.bottom, bar.height);
                const int w = t.right - t.left;
                OBL_CHECK(w > 0);
                OBL_CHECK(w <= 64);
                OBL_CHECK_EQ(t.depth, 0xffffu);
                x += w;
            }
            OBL_CHECK_EQ(x, width - bar.rightCorner); // the last tile stops at the right corner, no overdraw
        }
        OBL_CHECK_EQ(canvas.Live(), 0);
    }
}

OBL_TEST_CASE("gui_modes_tools: a screen taller than 960 tiles a mid-bar slice down the side bars to the lower corners")
{
    for (int height : {1080, 1200, 2160}) {
        RecordingCanvas canvas([](const char* n) { return RealisticMetricsOf(n); });
        DrawCameraFrame(canvas, 1920, height, 0.0f);
        struct Bar {
            const char* name;
            int x; // the bar's own x
            int width;
            int y0; // where the bar sprite ends
            int lowerCorner;
        };
        for (const Bar& bar : {Bar{"ui3_cam_c_left", 0, 30, 60 + 820, 80}, Bar{"ui3_cam_c_right", 1920 - 32, 32, 70 + 840, 50}}) {
            const std::vector<Call> tiles = BarDraws(canvas, bar.name);
            OBL_REQUIRE(!tiles.empty());
            int y = bar.y0;
            for (const Call& t : tiles) {
                OBL_CHECK_EQ(t.x, bar.x);
                OBL_CHECK_EQ(t.left, 0);
                OBL_CHECK_EQ(t.right, bar.width);
                OBL_CHECK_EQ(t.y, y); // the first drawn row lands at y whatever `top` skips
                const int h = t.bottom - t.top;
                OBL_CHECK(h > 0);
                OBL_CHECK(h <= 64);
                OBL_CHECK(t.top >= 200 && t.bottom <= 620);
                y += h;
            }
            OBL_CHECK_EQ(y, height - bar.lowerCorner);
        }
        OBL_CHECK_EQ(canvas.Live(), 0);
    }
}

OBL_TEST_CASE("gui_modes_tools: below 800 only the corners are drawn")
{
    for (int width : {640, 320}) {
        RecordingCanvas canvas(&MetricsOf);
        DrawCameraFrame(canvas, width, 480, 0.0f);
        const std::vector<Call> draws = Draws(canvas);
        OBL_CHECK_EQ(draws.size(), size_t(4));
        OBL_CHECK_EQ(canvas.created, 4);
        OBL_CHECK_EQ(canvas.Live(), 0);
    }
}

OBL_TEST_CASE("gui_modes_tools: the album keeps one thumbnail per window of the width's film strip")
{
    // 6 below 800, then 8 / 11 / 15 for the sets A / B / C; every width of a set shares its count.
    for (int width : {320, 640, 799}) OBL_CHECK_EQ(AlbumPhotoLimit(width), 6);
    for (int width : {800, 1000, 1023}) OBL_CHECK_EQ(AlbumPhotoLimit(width), 8);
    for (int width : {1024, 1200, 1279}) OBL_CHECK_EQ(AlbumPhotoLimit(width), 11);
    for (int width : {1280, 1366, 1600, 1920, 3840}) OBL_CHECK_EQ(AlbumPhotoLimit(width), 15);
    // The limit is the frame set's: no width is allowed more windows than the strip it draws has.
    for (int width = 320; width <= 4000; width += 7)
        OBL_CHECK_EQ(AlbumPhotoLimit(width),
                SelectCameraFrameSet(width) == CameraFrameSet::None        ? 6
                        : SelectCameraFrameSet(width) == CameraFrameSet::A ? 8
                        : SelectCameraFrameSet(width) == CameraFrameSet::B ? 11
                                                                           : 15);
}

OBL_TEST_CASE("gui_modes_tools: the album rule differs only at the widths outside its table")
{
    OBL_CHECK_EQ(AlbumPhotoLimitFor(640), 6);
    OBL_CHECK_EQ(AlbumPhotoLimitFor(800), 8);
    OBL_CHECK_EQ(AlbumPhotoLimitFor(1024), 11);
    OBL_CHECK_EQ(AlbumPhotoLimitFor(1280), 15);
    for (int width : {640, 800, 1024, 1280}) OBL_CHECK_EQ(AlbumPhotoLimitFor(width), AlbumPhotoLimit(width));
    for (int width : {1366, 1600, 1920, 900, 1100}) OBL_CHECK_EQ(AlbumPhotoLimitFor(width), 6);
}

OBL_TEST_CASE("gui_modes_tools: the album drops the oldest thumbnails so the new one ends at the limit")
{
    OBL_CHECK_EQ(AlbumDropCount(0, 8), 0);
    OBL_CHECK_EQ(AlbumDropCount(6, 8), 0);
    OBL_CHECK_EQ(AlbumDropCount(7, 8), 0);
    OBL_CHECK_EQ(AlbumDropCount(8, 8), 1); // a full strip loses one
    OBL_CHECK_EQ(AlbumDropCount(9, 8), 2); // an over-full list (the width shrank) is trimmed down to limit - 1, then the new one is added
    OBL_CHECK_EQ(AlbumDropCount(15, 6), 10);
    // Seventeen photos in a row never leave more than the limit.
    for (int limit : {6, 8, 11, 15}) {
        int count = 0;
        for (int shot = 0; shot < 17; ++shot) {
            count -= AlbumDropCount(count, limit);
            ++count;
            OBL_CHECK(count <= limit);
        }
        OBL_CHECK_EQ(count, limit);
    }
}

OBL_TEST_CASE("gui_modes_tools: the comment key is CAMERA_COMMENT_ plus the roll modulo 35")
{
    OBL_CHECK_EQ(AlbumCommentKey(0), std::string("CAMERA_COMMENT_0"));
    OBL_CHECK_EQ(AlbumCommentKey(34), std::string("CAMERA_COMMENT_34"));
    OBL_CHECK_EQ(AlbumCommentKey(35), std::string("CAMERA_COMMENT_0"));
    OBL_CHECK_EQ(AlbumCommentKey(32767), std::string("CAMERA_COMMENT_") + std::to_string(32767 % 35));
}

OBL_TEST_CASE("gui_modes_tools: the album page puts the logos on the bottom corners and the comment above the edge")
{
    const auto metrics = [](const char* name) {
        if (std::string(name) == "photo_olympus") return SpriteMetrics{1, 150, 40};
        if (std::string(name) == "photo_beachlife") return SpriteMetrics{1, 200, 60};
        if (std::string(name) == "photo_eidosdr") return SpriteMetrics{1, 90, 30};
        return SpriteMetrics{};
    };
    RecordingCanvas canvas(metrics);
    const obl::Char16* comment = u"Smile";
    int asked = 0;
    const int width = AlbumSurfaceWidth(1600), height = 900;
    OBL_CHECK_EQ(width, 1600 - 0x5e);
    DrawAlbumOverlay(canvas, width, height, [&] {
        OBL_CHECK_EQ(canvas.released, 3);
        OBL_CHECK_EQ(Draws(canvas).size(), size_t(3));
        OBL_CHECK(canvas.calls.back().kind != Call::DrawText);
        ++asked;
        return comment;
    });
    OBL_CHECK_EQ(asked, 1);
    const std::vector<Call> draws = Draws(canvas);
    OBL_REQUIRE(draws.size() == 3);
    const char* names[] = {"photo_olympus", "photo_beachlife", "photo_eidosdr"};
    const int xs[] = {0x14, width - 200 - 0x14, width - 90 - 0x61};
    const int ys[] = {height - 40 - 0x14, height - 60 - 0x14, height - 30 - 0x14};
    for (int i = 0; i < 3; ++i) {
        OBL_CHECK_EQ(draws[i].name, std::string(names[i]));
        OBL_CHECK_EQ(draws[i].x, xs[i]);
        OBL_CHECK_EQ(draws[i].y, ys[i]);
        OBL_CHECK_EQ(draws[i].depth, uint32_t(0xffff));
        OBL_CHECK(draws[i].time == obl::gui_canvas::kClippedTime);
    }
    const Call& text = canvas.calls.back();
    OBL_CHECK(text.kind == Call::DrawText);
    OBL_CHECK(text.wrapped);
    OBL_CHECK_EQ(text.renderer, kAlbumTextRenderer);
    OBL_CHECK_EQ(text.state.align, 2);
    OBL_CHECK(!text.state.outlined);
    OBL_CHECK_EQ(text.state.color, uint32_t(0));
    OBL_CHECK_EQ(text.state.x, 0);
    OBL_CHECK_EQ(text.state.y, height - 0x96);
    OBL_CHECK_EQ(text.state.width, width);
    OBL_CHECK_EQ(text.state.height, height - 0x82);
    OBL_CHECK(text.text == obl::Text16(u"Smile"));
    OBL_CHECK_EQ(canvas.Live(), 0);
}

OBL_TEST_CASE("gui_modes_tools: the paint mode multiplies the cursor by the zoom scale, truncates and adds the camera corner")
{
    OBL_CHECK_EQ(PaintMapPixel(100, 1.0f, 0), 100);
    OBL_CHECK_EQ(PaintMapPixel(100, 1.0f, 640), 740);
    OBL_CHECK_EQ(PaintMapPixel(100, 2.0f, 5), 205);
    OBL_CHECK_EQ(PaintMapPixel(101, 0.5f, 0), 50); // truncation toward zero, not rounding
    OBL_CHECK_EQ(PaintMapPixel(-101, 0.5f, 0), -50);
    OBL_CHECK_EQ(PaintMapPixel(799, 1.25f, 10), 1008); // 998.75 -> 998
    OBL_CHECK_EQ(PaintMapPixel(7, 0.1f, 0), 0); // 0.1f * 7 = 0.7000000089...
    OBL_CHECK_EQ(PaintMapPixel(30, 0.1f, 0), 3); // 0.1f is slightly above 0.1, so 3.0000000447 truncates to 3
}

OBL_TEST_CASE("gui_modes_tools: Esc opens the options only when no dialog is open")
{
    OBL_CHECK(EscapeOpensOptions(0));
    OBL_CHECK(!EscapeOpensOptions(1));
    OBL_CHECK(!EscapeOpensOptions(3));
}

OBL_TEST_CASE("gui_modes_tools: OnMouse divides the cursor by the zoom scale, truncates and adds the camera corner")
{
    OBL_CHECK_EQ(MouseMapPixel(100, 1.0f, 0), 100);
    OBL_CHECK_EQ(MouseMapPixel(100, 2.0f, 5), 55);
    OBL_CHECK_EQ(MouseMapPixel(101, 2.0f, 0), 50); // truncation toward zero
    OBL_CHECK_EQ(MouseMapPixel(-101, 2.0f, 0), -50);
    OBL_CHECK_EQ(MouseMapPixel(100, 0.5f, 640), 840);
    OBL_CHECK_EQ(MouseMapPixel(799, 1.25f, 10), 649); // 639.2 -> 639
}

OBL_TEST_CASE("gui_modes_tools: the dozer shows the bulldozer over the map and removes only on the map where something can go")
{
    OBL_CHECK_EQ(DozerCursorShape(true), kCursorArrow);
    OBL_CHECK_EQ(DozerCursorShape(false), kCursorDozer);
    OBL_CHECK(DozerRemoves(true, true));
    OBL_CHECK(!DozerRemoves(true, false));
    OBL_CHECK(!DozerRemoves(false, true));
    OBL_CHECK_EQ(std::string(DozerClickSound(true)), "Cash");
    OBL_CHECK_EQ(std::string(DozerClickSound(false)), "click_bad_01");
}

OBL_TEST_CASE("gui_modes_tools: the send-staff cursor follows the panel, then the target flag, then the tile")
{
    OBL_CHECK_EQ(SendStaffCursorShape(true, true, true), kCursorArrow);
    OBL_CHECK_EQ(SendStaffCursorShape(false, true, false), kCursorStaffTarget);
    OBL_CHECK_EQ(SendStaffCursorShape(false, false, true), kCursorStaffOk);
    OBL_CHECK_EQ(SendStaffCursorShape(false, false, false), kCursorStaffRefused);
    OBL_CHECK(SendStaffSends(1, true, true));
    OBL_CHECK(!SendStaffSends(2, true, true));
    OBL_CHECK(!SendStaffSends(1, false, true));
    OBL_CHECK(!SendStaffSends(1, true, false));
}

OBL_TEST_CASE("gui_modes_tools: the default mode points at an object only when nothing blocks the cursor")
{
    OBL_CHECK_EQ(DefaultCursorShape(false, false, true), kCursorOverObject);
    OBL_CHECK_EQ(DefaultCursorShape(false, false, false), kCursorArrow);
    OBL_CHECK_EQ(DefaultCursorShape(true, false, true), kCursorArrow);
    OBL_CHECK_EQ(DefaultCursorShape(false, true, true), kCursorArrow);
}

OBL_TEST_CASE("gui_modes_tools: the dozer and send-staff highlight is green where the mode may act and red elsewhere")
{
    const TileHighlightRgb ok = TileHighlightColor(true), bad = TileHighlightColor(false);
    OBL_CHECK(ok.r == 0 && ok.g == 255 && ok.b == 0);
    OBL_CHECK(bad.r == 255 && bad.g == 0 && bad.b == 0);
}

OBL_TEST_CASE("gui_modes_tools: the selection corner is the press pixel times the zoom scale, truncated")
{
    OBL_CHECK_EQ(SelectionScreenPoint(100, 1.0f), 100);
    OBL_CHECK_EQ(SelectionScreenPoint(101, 0.5f), 50);
    OBL_CHECK_EQ(SelectionScreenPoint(101, 2.0f), 202);
    OBL_CHECK_EQ(SelectionScreenPoint(-3, 0.5f), -1);
}

OBL_TEST_CASE("gui_modes_tools: the selection rectangle shows after the cursor leaves a 5 px square and while its corner is on screen")
{
    SelectionFrame f = ComputeSelectionFrame(100, 100, 105, 95, 800, 600);
    OBL_CHECK(!f.shown); // exactly 5 px away on both axes is still a click
    f = ComputeSelectionFrame(100, 100, 106, 100, 800, 600);
    OBL_CHECK(f.shown);
    OBL_CHECK(f.x1 == 100 && f.y1 == 100 && f.x2 == 106 && f.y2 == 100);
    f = ComputeSelectionFrame(100, 100, 90, 70, 800, 600);
    OBL_CHECK(f.shown);
    OBL_CHECK(f.x2 == 90 && f.y2 == 70);
    OBL_CHECK(!ComputeSelectionFrame(0, 100, 300, 300, 800, 600).shown); // the corner must be strictly inside the screen
    OBL_CHECK(!ComputeSelectionFrame(800, 100, 300, 300, 800, 600).shown);
    OBL_CHECK(!ComputeSelectionFrame(100, 0, 300, 300, 800, 600).shown);
    OBL_CHECK(!ComputeSelectionFrame(100, 600, 300, 300, 800, 600).shown);
    OBL_CHECK(ComputeSelectionFrame(799, 599, 300, 300, 800, 600).shown);
}

OBL_TEST_CASE("gui_modes_tools: the dozer clears surface bits, then bare path tiles without a building next to them")
{
    int asked = 0;
    auto free = [&] {
        ++asked;
        return true;
    };
    auto blocked = [&] {
        ++asked;
        return false;
    };
    OBL_CHECK(DozerCanRemove(false, false, 0x1000, free));
    OBL_CHECK_EQ(asked, 0); // the surface bits settle it without the neighbours
    const uint32_t path = 0x1 | 0x80000000u | 0x10000000u;
    OBL_CHECK(DozerCanRemove(false, false, path, free));
    OBL_CHECK_EQ(asked, 1);
    OBL_CHECK(!DozerCanRemove(false, false, path, blocked));
    OBL_CHECK(!DozerCanRemove(false, false, 0x1 | 0x80000000u, free)); // all three bits are needed
    OBL_CHECK_EQ(asked, 2);
    OBL_CHECK(!DozerCanRemove(true, false, 0x7000, free)); // any object on the tile hides the flag rules
    OBL_CHECK(DozerCanRemove(true, true, 0, blocked));
    OBL_CHECK(DozerCanRemove(false, true, 0, blocked));
}

OBL_TEST_CASE("gui_modes_tools: the dozer's masks and income")
{
    OBL_CHECK_EQ(ClearBarePath(0xffffffffu), 0xefffff7fu);
    OBL_CHECK_EQ(ClearSurface(0xffffffffu), 0xffff8fffu);
    OBL_CHECK(IsBarePathTile(0x90000001u));
    OBL_CHECK(!IsBarePathTile(0x80000001u));
    OBL_CHECK_EQ(SceneryRemovalIncome(100), 75.0);
    OBL_CHECK_EQ(SceneryRemovalIncome(0xffffffffu), 4294967295.0 * 0.75); // the cost is unsigned
}

OBL_TEST_CASE("gui_modes_tools: the send-staff rule follows the member's name")
{
    OBL_CHECK(ClassifyStaffTarget("BUILDER") == StaffRule::Builder);
    OBL_CHECK(ClassifyStaffTarget("CLEANER") == StaffRule::Cleaner);
    OBL_CHECK(ClassifyStaffTarget("SECURITYGUARD") == StaffRule::SecurityGuard);
    for (const char* name : {"LIFEG_F", "LIFEG_M", "REP_M", "REP_F"}) OBL_CHECK(ClassifyStaffTarget(name) == StaffRule::Guests);
    OBL_CHECK(ClassifyStaffTarget("MECHANIC") == StaffRule::Mechanic);
    OBL_CHECK(ClassifyStaffTarget("builder") == StaffRule::Builder);
    OBL_CHECK(ClassifyStaffTarget("BUILDERS") == StaffRule::None);
    OBL_CHECK(ClassifyStaffTarget("") == StaffRule::None);
    OBL_CHECK(StaffTileAllowed(0x80000000u));
    OBL_CHECK(!StaffTileAllowed(0x80000010u));
    OBL_CHECK(!StaffTileAllowed(0x1));
}

OBL_TEST_CASE("gui_modes_tools: selection levels and the rectangle's corner order")
{
    OBL_CHECK(SelectionLevelWanted(true, 1) && SelectionLevelWanted(true, 2) && !SelectionLevelWanted(true, 0));
    OBL_CHECK(SelectionLevelWanted(false, 0) && !SelectionLevelWanted(false, 1));
    OBL_CHECK(IsStaffType(7) && IsStaffType(12) && !IsStaffType(6) && !IsStaffType(13));
    float low, high;
    OrderSelectionAxis(1.0f, 2.0f, low, high);
    OBL_CHECK(low == 1.0f && high == 2.0f);
    OrderSelectionAxis(5.0f, 2.0f, low, high);
    OBL_CHECK(low == 2.0f && high == 5.0f);
    OrderSelectionAxis(3.0f, 3.0f, low, high);
    OBL_CHECK(low == 3.0f && high == 3.0f);
    OrderSelectionAxis(std::numeric_limits<float>::quiet_NaN(), 4.0f, low, high); // unordered counts as "press first"
    OBL_CHECK(low != low && high == 4.0f);
}
