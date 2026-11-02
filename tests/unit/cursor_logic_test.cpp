
#include "unit_test.h"

#include "game/ui/cursor_logic.h"
#include "game/ui/cursor_types.h"

using namespace obl::cursor;

OBL_TEST_CASE("cursor: the phase advances by delta times speed and wraps below 8.0")
{
    OBL_CHECK(AdvancePhase(0.0f, 0.1f, 10.0f) == 1.0f);
    OBL_CHECK(AdvancePhase(7.5f, 0.1f, 10.0f) == 0.5f);
    OBL_CHECK(AdvancePhase(0.0f, 1.0f, 10.0f) == 2.0f); // 10 wraps once
    OBL_CHECK(AdvancePhase(0.0f, 2.5f, 10.0f) == 1.0f); // 25 wraps three times
    OBL_CHECK(AdvancePhase(3.0f, 0.0f, 10.0f) == 3.0f);
    OBL_CHECK(AdvancePhase(0.0f, 0.8f, 10.0f) < 8.0f);
}

OBL_TEST_CASE("cursor: the rectangle is closed on all four sides")
{
    OBL_CHECK(InRect(10, 20, 10, 20, 5, 5));
    OBL_CHECK(InRect(15, 25, 10, 20, 5, 5));
    OBL_CHECK(!InRect(16, 25, 10, 20, 5, 5));
    OBL_CHECK(!InRect(15, 26, 10, 20, 5, 5));
    OBL_CHECK(!InRect(9, 20, 10, 20, 5, 5));
    OBL_CHECK(!InRect(10, 19, 10, 20, 5, 5));
    OBL_CHECK(InRect(3, 3, 3, 3, 0, 0));
}

OBL_TEST_CASE("cursor: the position is divided by the zoom and truncated toward zero")
{
    int out[2];
    WorldPos(401, 299, 2.0f, out);
    OBL_CHECK(out[0] == 200);
    OBL_CHECK(out[1] == 149);
    WorldPos(401, 299, 0.5f, out);
    OBL_CHECK(out[0] == 802);
    OBL_CHECK(out[1] == 598);
    WorldPos(-300, -300, 1.0f, out);
    OBL_CHECK(out[0] == -300);
    WorldPos(-301, 5, 2.0f, out);
    OBL_CHECK(out[0] == -150);
    WorldPos(5, 5, 0.0f, out);
    OBL_CHECK(out[0] == INT32_MIN);
}

OBL_TEST_CASE("cursor: shapes 0..6 map to Input shapes 1..7, others leave the Input shape alone")
{
    for (int shape = 0; shape <= 6; ++shape) OBL_CHECK(InputShape(shape) == shape + 1);
    OBL_CHECK(InputShape(7) == 0);
    OBL_CHECK(InputShape(-1) == 0);
}

OBL_TEST_CASE("cursor: the render cell is a 32x32 square at shape column and frame row")
{
    const auto cell = RenderCell(3, 5);
    OBL_CHECK(cell.left == 96);
    OBL_CHECK(cell.top == 160);
    OBL_CHECK(cell.right == 127);
    OBL_CHECK(cell.bottom == 191);
    OBL_CHECK(RenderStateLegal(7, 7));
    OBL_CHECK(!RenderStateLegal(8, 0));
    OBL_CHECK(!RenderStateLegal(0, 8));
    OBL_CHECK(Visible(-300, 5));
    OBL_CHECK(!Visible(-300, -300));
}
