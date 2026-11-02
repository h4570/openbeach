#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "unit_test.h"
#include "game/objects/guest_draw.h"

#include <array>
#include <vector>

namespace {
struct Env {
    float zoom = 1;
    int stat = 1, sprites = 0;
    std::vector<std::array<int, 4>> rects, lines;
    float Zoom() { return zoom; }
    int Stat() { return stat; }
    uint32_t Color(uint32_t r, uint32_t g, uint32_t b) { return r * 65536 + g * 256 + b; }
    void Fill(int x, int y, int w, int h, uint32_t) { rects.push_back({x, y, w, h}); }
    void Line(int x, int y, int x1, int y1, uint32_t) { lines.push_back({x, y, x1, y1}); }
    void Clipped(obl::ui::SpriteInstance*, int, int) { ++sprites; }
};
} // namespace

OBL_TEST_CASE("guest draw: stat selector reads each distinct guest field")
{
    obl::object::Guest g{};
    g.m_happiness = 0.2f;
    g.m_sunBurnLevel = -0.4f;
    g.m_desires[4] = 0.6f;
    g.m_drunk = 0.8f;
    g.m_desires[14] = 1;
    g.m_foodIll = 0;
    for (int i = 0; i < 7; ++i) g.m_opinion[i] = float(i + 1) / 8;
    constexpr int widths[] = {7, 14, 21, 28, 35, 1, 17, 30, 8, 4, 26, 13, 21};
    constexpr int selectors[] = {1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    for (int i = 0; i < 13; ++i) {
        Env env;
        env.stat = selectors[i];
        int p[2] = {100, 200};
        obl::guest_draw::StatBar(g, p, env);
        OBL_CHECK_EQ(env.rects.size(), size_t(2));
        OBL_CHECK_EQ(env.rects[0][2], widths[i]);
        OBL_CHECK_EQ(env.rects[1][2], 35 - widths[i]);
        OBL_CHECK_EQ(p[0], 75);
        OBL_CHECK_EQ(p[1], 200);
        OBL_CHECK_EQ(env.rects[0][1], 250);
    }
}

OBL_TEST_CASE("guest draw: bars keep unscaled width and do not clamp negative values")
{
    obl::object::Guest g{};
    g.m_happiness = -0.2f;
    Env env;
    env.zoom = 0.5f;
    int p[2] = {-101, -203};
    obl::guest_draw::StatBar(g, p, env);
    OBL_CHECK_EQ(p[0], -62);
    OBL_CHECK_EQ(p[1], -101);
    OBL_CHECK_EQ(env.rects[0][2], -7);
    OBL_CHECK_EQ(env.rects[1][2], 42);
    OBL_CHECK_EQ(env.rects[0][3], 4);
}

OBL_TEST_CASE("guest draw: marker scales and truncates separately and mutates the screen point")
{
    Env env;
    env.zoom = 0.5f;
    int p[2] = {-101, 203};
    obl::ui::SpriteInstance s{};
    obl::guest_draw::Marker(p, 1, &s, env);
    OBL_CHECK_EQ(s.m_frameIndex, 4);
    OBL_CHECK_EQ(p[0], -38);
    OBL_CHECK_EQ(p[1], 101);
    OBL_CHECK_EQ(env.lines.size(), size_t(16));
    OBL_CHECK_EQ(env.sprites, 1);
    OBL_CHECK_EQ(env.lines[0][0], -61);
    OBL_CHECK_EQ(env.lines[0][1], 89);
    OBL_CHECK_EQ(env.lines[0][2], -54);
    OBL_CHECK_EQ(env.lines[4][1], 131);
}
