
#include "unit_test.h"

#include "game/ui/line_draw.h"

#include <vector>

using namespace obl;
using namespace obl::line_draw;

namespace {

constexpr int kW = 16, kH = 12;

std::vector<uint32_t> Scene()
{
    return std::vector<uint32_t>(kW * kH, 0xabcd0000u);
}
std::vector<uint16_t> Surface()
{
    return std::vector<uint16_t>(kW * kH, 0);
}

int Count32(const std::vector<uint32_t>& scene)
{
    int n = 0;
    for (uint32_t w : scene) n += (w & 0xffff) != 0;
    return n;
}

} // namespace

OBL_TEST_CASE("line_draw: the dispatcher rejects lines outside the view and swaps to the upper end first")
{
    Line line;
    OBL_CHECK(!Prepare(kW, kH, kW, 0, 3, 5, &line)); // start at the right edge
    OBL_CHECK(!Prepare(kW, kH, 3, 0, -1, 5, &line)); // end left of the view
    OBL_CHECK(!Prepare(kW, kH, 3, kH, 3, 5, &line)); // start below
    OBL_CHECK(!Prepare(kW, kH, 3, -4, 3, 0, &line)); // end not below the first row
    OBL_CHECK(Prepare(kW, kH, 9, 8, 2, 1, &line));
    OBL_CHECK(line.x == 2 && line.y == 1);
    OBL_CHECK(line.major == 7 && line.minor == 7 && line.step == 1);
    OBL_CHECK(!line.shallow); // equal extents are steep
    OBL_CHECK(Prepare(kW, kH, 2, 1, 9, 2, &line));
    OBL_CHECK(line.shallow && line.major == 7 && line.minor == 1);
    OBL_CHECK(Prepare(kW, kH, 9, 1, 2, 2, &line));
    OBL_CHECK(line.step == -1 && line.x == 9 && line.major == 7 && line.shallow);
    OBL_CHECK(Prepare(kW, kH, 4, 1, 4, 6, &line));
    OBL_CHECK(line.step == -1 && line.major == 0 && line.minor == 5 && !line.shallow); // a vertical line "steps left" but never moves
}

OBL_TEST_CASE("line_draw: scene lines keep the depth half and include both ends")
{
    auto scene = Scene();
    Line line;
    OBL_REQUIRE(Prepare(kW, kH, 1, 2, 8, 2, &line));
    Draw32(line, 0x12345678u, scene.data(), kW, kW, kH);
    for (int x = 1; x <= 8; ++x) OBL_CHECK(scene[2 * kW + x] == 0xabcd5678u);
    OBL_CHECK(Count32(scene) == 8);

    scene = Scene();
    OBL_REQUIRE(Prepare(kW, kH, 3, 1, 3, 6, &line));
    Draw32(line, 7, scene.data(), kW, kW, kH);
    for (int y = 1; y <= 6; ++y) OBL_CHECK(scene[y * kW + 3] == 0xabcd0007u);
    OBL_CHECK(Count32(scene) == 6);

    scene = Scene();
    OBL_REQUIRE(Prepare(kW, kH, 0, 0, 5, 5, &line));
    Draw32(line, 9, scene.data(), kW, kW, kH);
    for (int i = 0; i <= 5; ++i) OBL_CHECK(scene[i * kW + i] == 0xabcd0009u);
    OBL_CHECK(Count32(scene) == 6);

    scene = Scene();
    OBL_REQUIRE(Prepare(kW, kH, 8, 1, 2, 7, &line)); // up-right to down-left
    Draw32(line, 9, scene.data(), kW, kW, kH);
    for (int i = 0; i <= 6; ++i) OBL_CHECK(scene[(1 + i) * kW + 8 - i] == 0xabcd0009u);
    OBL_CHECK(Count32(scene) == 7);
}

OBL_TEST_CASE("line_draw: clipping is an unsigned compare against the scene size")
{
    auto scene = Scene();
    Line line;
    OBL_REQUIRE(Prepare(kW, kH, -5, 3, 30, 3, &line)); // the start is left of the scene: only 0..15 are drawn
    Draw32(line, 5, scene.data(), kW, kW, kH);
    OBL_CHECK(Count32(scene) == kW);
    scene = Scene();
    OBL_REQUIRE(Prepare(kW, kH, 4, -3, 4, 5, &line)); // starts above the scene
    Draw32(line, 5, scene.data(), kW, kW, kH);
    OBL_CHECK(Count32(scene) == 6); // rows 0..5 (y -3..5 minus the three above the scene)
}

OBL_TEST_CASE("line_draw: the back-buffer shallow loop stops one short of the edges, the steep loop does not")
{
    auto surface = Surface();
    Line line;
    OBL_REQUIRE(Prepare(kW + 20, kH + 20, 0, 4, kW + 3, 4, &line));
    Draw16(line, 0xf81f, surface.data(), kW, kW, kH);
    int n = 0;
    for (uint16_t p : surface) n += p == 0xf81f;
    OBL_CHECK(n == kW - 1); // the walk is clipped at width - 1
    OBL_CHECK(surface[4 * kW + kW - 2] == 0xf81f);
    OBL_CHECK(surface[4 * kW + kW - 1] == 0);

    surface = Surface();
    OBL_REQUIRE(Prepare(kW + 20, kH + 20, 4, 0, 4, kH + 3, &line));
    Draw16(line, 0xf81f, surface.data(), kW, kW, kH);
    n = 0;
    for (uint16_t p : surface) n += p == 0xf81f;
    OBL_CHECK(n == kH); // rows 0..11, the bottom row included
    OBL_CHECK(surface[(kH - 1) * kW + 4] == 0xf81f);
}

OBL_TEST_CASE("line_draw: a single point is drawn once")
{
    auto scene = Scene();
    Line line;
    OBL_REQUIRE(Prepare(kW, kH, 6, 3, 6, 3, &line));
    Draw32(line, 1, scene.data(), kW, kW, kH);
    OBL_CHECK(Count32(scene) == 1);
    OBL_CHECK(scene[3 * kW + 6] == 0xabcd0001u);
}
