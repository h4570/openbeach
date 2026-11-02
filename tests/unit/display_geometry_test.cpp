// Letterbox geometry shared by the SDL/GDI presenters, the local cursor and mouse mapping.

#include "core/char16.h"
#include "unit_test.h"

#include "game/ui/display_geometry.h"

using namespace obl::ui;

OBL_TEST_CASE("geometry.image_rect_exact_scale")
{
    obl::screen::Rect r = ImageRect(800, 600, 1200, 900);
    OBL_CHECK_EQ(r.left, 0);
    OBL_CHECK_EQ(r.top, 0);
    OBL_CHECK_EQ(r.right, 1200);
    OBL_CHECK_EQ(r.bottom, 900);
}

OBL_TEST_CASE("geometry.image_rect_pillarbox_and_letterbox")
{
    obl::screen::Rect wide = ImageRect(800, 600, 1920, 1080); // 1440x1080, bars left and right
    OBL_CHECK_EQ(wide.left, 240);
    OBL_CHECK_EQ(wide.right, 1680);
    OBL_CHECK_EQ(wide.top, 0);
    OBL_CHECK_EQ(wide.bottom, 1080);

    obl::screen::Rect tall = ImageRect(800, 600, 800, 1000); // 800x600, bars top and bottom
    OBL_CHECK_EQ(tall.left, 0);
    OBL_CHECK_EQ(tall.right, 800);
    OBL_CHECK_EQ(tall.top, 200);
    OBL_CHECK_EQ(tall.bottom, 800);
}

OBL_TEST_CASE("geometry.image_rect_degenerate_sizes")
{
    // A minimised window reports a 0x0 client area; the rect must stay non-empty (no division by zero later).
    obl::screen::Rect r = ImageRect(800, 600, 0, 0);
    OBL_CHECK(r.right > r.left);
    OBL_CHECK(r.bottom > r.top);
}

OBL_TEST_CASE("geometry.game_client_roundtrip")
{
    const DisplaySize games[] = {{800, 600}, {1024, 768}, {1280, 1024}};
    const DisplaySize outputs[] = {{800, 600}, {1000, 750}, {1200, 900}, {1920, 1080}, {1000, 1000}, {3840, 2160}};
    for (DisplaySize game : games) {
        for (DisplaySize output : outputs) {
            obl::screen::Rect image = ImageRect(game.cx, game.cy, output.cx, output.cy);
            if (image.right - image.left < game.cx) continue; // shrinking cannot round-trip every pixel
            for (int x = 0; x < game.cx; ++x) {
                DisplayPoint client = MapGameToClient({x, 0}, game, image);
                DisplayPoint back = MapClientToGame(client, game, image);
                if (back.x != x) {
                    OBL_CHECK_EQ(back.x, x);
                    return;
                }
            }
            for (int y = 0; y < game.cy; ++y) {
                DisplayPoint back = MapClientToGame(MapGameToClient({0, y}, game, image), game, image);
                if (back.y != y) {
                    OBL_CHECK_EQ(back.y, y);
                    return;
                }
            }
        }
    }
}

OBL_TEST_CASE("geometry.client_outside_image_stays_outside")
{
    // Clicks on the bars must not land on an edge control of the game.
    DisplaySize game{800, 600};
    obl::screen::Rect image = ImageRect(800, 600, 1920, 1080);
    OBL_CHECK(MapClientToGame({image.left - 1, 500}, game, image).x < 0);
    OBL_CHECK(MapClientToGame({image.right, 500}, game, image).x >= game.cx);
    OBL_CHECK_EQ(MapClientToGame({image.left, 0}, game, image).x, 0);
    OBL_CHECK_EQ(MapClientToGame({image.right - 1, 1079}, game, image).x, 799);
    OBL_CHECK_EQ(MapClientToGame({image.right - 1, 1079}, game, image).y, 599);
}

OBL_TEST_CASE("geometry.client_to_game_integer_scales")
{
    // At 1x and 2x every client pixel maps to the game pixel that contains it.
    DisplaySize game{800, 600};
    obl::screen::Rect one = ImageRect(800, 600, 800, 600);
    obl::screen::Rect two = ImageRect(800, 600, 1600, 1200);
    for (int c = 0; c < 1600; ++c) {
        if (c < 800 && MapClientToGame({c, 0}, game, one).x != c) {
            OBL_CHECK_EQ(MapClientToGame({c, 0}, game, one).x, c);
            return;
        }
        if (MapClientToGame({c, 0}, game, two).x != c / 2) {
            OBL_CHECK_EQ(MapClientToGame({c, 0}, game, two).x, c / 2);
            return;
        }
    }
}

OBL_TEST_CASE("geometry.parse_scale")
{
    double scale = 0;
    OBL_CHECK(ParseScale(u"1", &scale) && scale == 1.0);
    OBL_CHECK(ParseScale(u"1.5", &scale) && scale == 1.5);
    OBL_CHECK(ParseScale(u"4", &scale) && scale == 4.0);
    scale = 2.0;
    // Out of range, trailing text, empty and non-finite values are rejected and leave the output alone.
    for (const obl::Char16* bad : {u"0.99", u"4.01", u"-2", u"", u"2x", u"1.5 ", u"abc", u"nan", u"inf"})
        OBL_CHECK(!ParseScale(bad, &scale));
    OBL_CHECK_EQ(scale, 2.0);
}

OBL_TEST_CASE("geometry.scaled_client_size")
{
    DisplaySize s = ScaledClientSize(800, 600, 1.5, 1920, 1080);
    OBL_CHECK_EQ(s.cx, 1200);
    OBL_CHECK_EQ(s.cy, 900);
    OBL_CHECK(!CoversMonitor(s, 1920, 1080));
    s = ScaledClientSize(1024, 768, 1.25, 1920, 1080); // 1280x960, rounded
    OBL_CHECK_EQ(s.cx, 1280);
    OBL_CHECK_EQ(s.cy, 960);
    s = ScaledClientSize(1022, 767, 1.5, 1920, 1080); // 1533x1150.5 -> clamped height, rounded width
    OBL_CHECK_EQ(s.cx, 1533);
    OBL_CHECK_EQ(s.cy, 1080);
    OBL_CHECK(!CoversMonitor(s, 1920, 1080)); // one dimension alone stays a framed window
    s = ScaledClientSize(1600, 900, 2.0, 1920, 1080);
    OBL_CHECK_EQ(s.cx, 1920);
    OBL_CHECK_EQ(s.cy, 1080);
    OBL_CHECK(CoversMonitor(s, 1920, 1080));
    OBL_CHECK(CoversMonitor(ScaledClientSize(1920, 1080, 1.0, 1920, 1080), 1920, 1080)); // native = borderless
}
