#include "unit_test.h"
#include "game/ui/gui_radar.h"

using namespace obl::gui_radar;

OBL_TEST_CASE("Radar camera rectangle scales the screen over the map onto the widget")
{
    // 1024x768 screen, 4096x4096 map: 1024 / 3968 * 100 and (768 - 88) / 4032 * 80, both truncated, plus the widget corner.
    const ViewRect view = CalcViewRect({10, 20, 110, 100}, 1024, 768, 4096, 4096);
    OBL_CHECK(view.x0 == 10);
    OBL_CHECK(view.y0 == 20);
    OBL_CHECK(view.x1 == 10 + 25);
    OBL_CHECK(view.y1 == 20 + 13);
}

OBL_TEST_CASE("Radar camera rectangle takes the smaller GUI bar below 800 pixels wide")
{
    const ViewRect small = CalcViewRect({0, 0, 100, 100}, 799, 600, 4096, 4096);
    const ViewRect large = CalcViewRect({0, 0, 100, 100}, 800, 600, 4096, 4096);
    OBL_CHECK(small.y1 == static_cast<int>((600 - 70.0) / 4032.0f * 100));
    OBL_CHECK(large.y1 == static_cast<int>((600 - 88.0) / 4032.0f * 100));
}

OBL_TEST_CASE("Radar camera rectangle with a degenerate map goes through the truncating conversion")
{
    const ViewRect view = CalcViewRect({5, 6, 105, 106}, 1024, 768, 128, 64);
    OBL_CHECK(view.x1 == 0);
    OBL_CHECK(view.y1 == 0);
}

OBL_TEST_CASE("Radar hover box uses the panel sprite height for the bottom edge")
{
    const Rect panel{100, 200, 300, 999};
    OBL_CHECK(PanelHovered(panel, 50, 100, 200));
    OBL_CHECK(PanelHovered(panel, 50, 300, 250));
    OBL_CHECK(!PanelHovered(panel, 50, 301, 250));
    OBL_CHECK(!PanelHovered(panel, 50, 200, 251));
    OBL_CHECK(!PanelHovered(panel, 50, 99, 220));
    OBL_CHECK(!PanelHovered(panel, 50, 200, 199));
}

OBL_TEST_CASE("Radar click targets the map point under the cursor, centred on the camera rectangle")
{
    // widget 100 x 100 at (10, 20), rectangle 20 x 10: click (60, 45) -> (60 - 10 - 10, 45 - 20 - 5) = (40, 20) of 100, scaled onto a 4096 map.
    const Rect widget{10, 20, 110, 120};
    const ViewRect view{10, 20, 30, 30};
    const Point middle = ClickCameraTarget(widget, view, 60, 45, 4096, 4096, 1, 1024, 768);
    OBL_CHECK(middle.x == static_cast<int>(40.0 / 100 * 4096));
    OBL_CHECK(middle.y == static_cast<int>(20.0 / 100 * 4096));
    const Point zoomedIn = ClickCameraTarget(widget, view, 60, 45, 4096, 4096, 0, 1024, 768);
    const Point zoomedOut = ClickCameraTarget(widget, view, 60, 45, 4096, 4096, 2, 1024, 768);
    OBL_CHECK(zoomedIn.x == middle.x + 256);
    OBL_CHECK(zoomedIn.y == middle.y + 192);
    OBL_CHECK(zoomedOut.x == middle.x - 256);
    OBL_CHECK(zoomedOut.y == middle.y - 192);
    // An empty widget divides by zero: the truncation gives 0.
    OBL_CHECK(ClickCameraTarget({0, 0, 0, 0}, view, 5, 5, 4096, 4096, 1, 1024, 768).x == 0);
}

OBL_TEST_CASE("Radar rectangle follows the float camera and keeps its size")
{
    const Rect widget{10, 20, 110, 120};
    const ViewRect view{0, 0, 25, 13};
    // The camera at the border (64, 32) puts the corner on the widget corner.
    const ViewRect home = ShiftedViewRect(widget, view, 64.f, 32.f, 4096, 4096);
    OBL_CHECK(home.x0 == 10);
    OBL_CHECK(home.y0 == 20);
    OBL_CHECK(home.x1 - home.x0 == 25);
    OBL_CHECK(home.y1 - home.y0 == 13);
    // Half the scrollable width: (1984 / 3968) * 100 = 50.
    const ViewRect middle = ShiftedViewRect(widget, view, 64.f + 1984.f, 32.f + 2016.f, 4096, 4096);
    OBL_CHECK(middle.x0 == 60);
    OBL_CHECK(middle.y0 == 70);
}

OBL_TEST_CASE("Radar camera rectangle lines depend on the zoom level")
{
    Line lines[4];
    const ViewRect view{10, 20, 50, 60};
    OBL_CHECK(CameraRectLines(view, 1, lines) == 4);
    OBL_CHECK(lines[0].x1 == 50);
    OBL_CHECK(lines[1].y0 == 60);
    OBL_CHECK(CameraRectLines(view, 2, lines) == 4);
    OBL_CHECK(lines[0].x1 == 90);
    OBL_CHECK(lines[3].y1 == 100);
    OBL_CHECK(CameraRectLines(view, 0, lines) == 4);
    OBL_CHECK(lines[0].x1 == 30);
    OBL_CHECK(lines[3].y1 == 40);
    OBL_CHECK(CameraRectLines(view, 3, lines) == 0);
}

OBL_TEST_CASE("Radar markers map the world onto the widget with half the world depth")
{
    const MarkerFrame frame{100.f, 50.f, 10.f, 20.f, 4096.f, 4096.f};
    const Point pixel = MarkerPixel(frame, 2048.f, 4096.f);
    OBL_CHECK(pixel.x == 60);
    OBL_CHECK(pixel.y == 20 + 25); // y 4096 halved = 2048 of 4096 -> 25 of 50
    const Rect square = MarkerSquare(frame, 2048.f, 4096.f);
    OBL_CHECK(square.x1 - square.x0 == 2);
    OBL_CHECK(square.y1 - square.y0 == 2);
    const Rect bar = MarkerFootprint(frame, 2048.f, 4096.f, 8, 4);
    OBL_CHECK(bar.x0 == 58);
    OBL_CHECK(bar.x1 == 62);
    OBL_CHECK(bar.y0 == 45 - 4);
    OBL_CHECK(bar.y1 == 45 - 2);
}

OBL_TEST_CASE("Progress bar sits above the bottom edge by the strip height")
{
    OBL_CHECK(ProgressBarY(1024, 768) == 768 - 0xbe);
    OBL_CHECK(ProgressBarY(799, 600) == 600 - 0xaa);
    OBL_CHECK(ProgressBarY(800, 600) == 600 - 0xbe);
    const Point big = ProgressSpritePos(9, 500, true);
    const Point small = ProgressSpritePos(9, 500, false);
    OBL_CHECK(big.y == 500);
    OBL_CHECK(small.y == 500 + 0x41);
}
