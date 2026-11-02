#include "game/weather/weather_state.h"
#include "unit_test.h"

#include "game/weather/weather_draw.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::weather;

namespace {

std::unique_ptr<weather::RainDropBuffer> NewDrops()
{
    auto drops = std::make_unique<weather::RainDropBuffer>();
    for (uint32_t i = 0; i < weather::kRainDropCount; ++i) {
        drops->m_xy[i][0] = (i * 7) % 100;
        drops->m_xy[i][1] = (i * 13) % 58;
    }
    return drops;
}

RainView View(int32_t state, bool rgb555 = true, bool paused = false)
{
    return {state, 100, 60, 100, rgb555, paused};
}

} // namespace

OBL_TEST_CASE("weather_draw.drop_count_doubles_above_600_rows")
{
    OBL_CHECK_EQ(RainDropCountBase(kRainLight, 600), 2000u);
    OBL_CHECK_EQ(RainDropCountBase(kRainLight, 601), 4000u);
    OBL_CHECK_EQ(RainDropCountBase(kRainHeavy, 480), 4000u);
    OBL_CHECK_EQ(RainDropCountBase(kRainStorm, 1080), 8000u);
}

OBL_TEST_CASE("weather_draw.scaled_drop_count_keeps_the_800x600_density")
{
    OBL_CHECK_EQ(RainDropCountScaled(kRainHeavy, 800, 600), 4000u);
    OBL_CHECK_EQ(RainDropCountScaled(kRainLight, 800, 600), 2000u);
    OBL_CHECK_EQ(RainDropCountScaled(kRainHeavy, 1920, 1080), 17280u);
    OBL_CHECK_EQ(RainDropCountScaled(kRainLight, 1920, 1080), 8640u);
    OBL_CHECK_EQ(RainDropCountScaled(kRainHeavy, 3840, 2160), 64000u); // capped at 8 laps of the drop buffer
}

OBL_TEST_CASE("weather_draw.scaled_drop_count_never_drops_below_the_base_count")
{
    OBL_CHECK_EQ(RainDropCountScaled(kRainHeavy, 1024, 768), 8000u);
    OBL_CHECK_EQ(RainDropCountScaled(kRainHeavy, 640, 480), 4000u);
    OBL_CHECK_EQ(RainDropCountScaled(kRainHeavy, 0, 0), 4000u);
}

OBL_TEST_CASE("weather_draw.dim_word_halves_the_colour_and_keeps_the_depth")
{
    OBL_CHECK_EQ(DimWord(0xabcd7fff, true), 0xabcd0000u + ((0x7fffu >> 1 & 0x3def) + 0x3def));
    OBL_CHECK_EQ(DimWord(0x12340000, false), 0x12340000u + 0x7bef);
    OBL_CHECK_EQ(DimWord(0xffffffff, false), 0xffff0000u + ((0xffffu >> 1 & 0x7bef) + 0x7bef));
}

OBL_TEST_CASE("weather_draw.light_rain_moves_one_by_eight_and_dims_two_rows")
{
    auto drops = NewDrops();
    std::vector<uint32_t> work(100 * 60, 0x0000ffff);
    drops->m_xy[0][0] = 99; // wraps to column 0
    drops->m_xy[0][1] = 50; // 58 % 58 = 0
    DrawRain(View(kRainLight), work.data(), *drops, 1);
    OBL_CHECK_EQ(drops->m_xy[0][0], 0u);
    OBL_CHECK_EQ(drops->m_xy[0][1], 0u);
    const uint32_t dim = DimWord(0xffff, true);
    OBL_CHECK_EQ(work[0], dim);
    OBL_CHECK_EQ(work[100], dim);
    OBL_CHECK_EQ(work[200], 0xffffu); // light rain is two rows tall
    OBL_CHECK_EQ(drops->m_xy[1][0], 7u); // the second drop did not move
}

OBL_TEST_CASE("weather_draw.heavy_rain_moves_two_by_fourteen_and_dims_three_rows")
{
    auto drops = NewDrops();
    std::vector<uint32_t> work(100 * 60, 0x0000ffff);
    drops->m_xy[0][0] = 10;
    drops->m_xy[0][1] = 20;
    DrawRain(View(kRainHeavy, false), work.data(), *drops, 1);
    OBL_CHECK_EQ(drops->m_xy[0][0], 12u);
    OBL_CHECK_EQ(drops->m_xy[0][1], 34u);
    const uint32_t dim = DimWord(0xffff, false);
    OBL_CHECK_EQ(work[34 * 100 + 12], dim);
    OBL_CHECK_EQ(work[35 * 100 + 12], dim);
    OBL_CHECK_EQ(work[36 * 100 + 12], dim);
    OBL_CHECK_EQ(work[37 * 100 + 12], 0xffffu);
}

OBL_TEST_CASE("weather_draw.paused_drops_stand_still_but_are_drawn")
{
    auto drops = NewDrops();
    std::vector<uint32_t> work(100 * 60, 0x0000ffff);
    drops->m_xy[0][0] = 10;
    drops->m_xy[0][1] = 20;
    DrawRain(View(kRainStorm, true, true), work.data(), *drops, 1);
    OBL_CHECK_EQ(drops->m_xy[0][0], 10u);
    OBL_CHECK_EQ(drops->m_xy[0][1], 20u);
    OBL_CHECK_EQ(work[20 * 100 + 10], DimWord(0xffff, true));
}

OBL_TEST_CASE("weather_draw.other_weather_and_degenerate_views_draw_nothing")
{
    auto drops = NewDrops();
    const auto reference = *drops;
    std::vector<uint32_t> work(100 * 60, 0x0000ffff);
    const std::vector<uint32_t> blank = work;
    for (int32_t state : {0, 1, 2, 6, 13}) DrawRain(View(state), work.data(), *drops, 4000);
    RainView empty = View(kRainHeavy);
    empty.viewWidth = 0;
    DrawRain(empty, work.data(), *drops, 4000);
    empty = View(kRainHeavy);
    empty.viewHeight = 2;
    DrawRain(empty, work.data(), *drops, 4000);
    OBL_CHECK(work == blank);
    OBL_CHECK(std::memcmp(drops.get(), &reference, sizeof(reference)) == 0);
}

OBL_TEST_CASE("weather_draw.extra_drops_of_a_scaled_count_ride_on_the_first_8000")
{
    // every drop starts on one spot, so the first 8000 only touch one column and the extras land a lap stride away
    auto drops = NewDrops(), dropsScaled = NewDrops();
    for (uint32_t i = 0; i < weather::kRainDropCount; ++i)
        drops->m_xy[i][0] = dropsScaled->m_xy[i][0] = 5, drops->m_xy[i][1] = dropsScaled->m_xy[i][1] = 5;
    std::vector<uint32_t> own(100 * 60, 0x0000ffff), scaled = own;
    DrawRain(View(kRainHeavy), own.data(), *drops, weather::kRainDropCount);
    DrawRain(View(kRainHeavy), scaled.data(), *dropsScaled, weather::kRainDropCount + 1);
    OBL_CHECK(std::memcmp(drops.get(), dropsScaled.get(), sizeof(weather::RainDropBuffer)) == 0); // only the first 8000 move
    const uint32_t x = (dropsScaled->m_xy[0][0] + kLapStrideX) % 100, y = (dropsScaled->m_xy[0][1] + kLapStrideY) % 58;
    OBL_CHECK_EQ(own[y * 100 + x], 0xffffu);
    OBL_CHECK_EQ(scaled[y * 100 + x], DimWord(0xffff, true));
    scaled[y * 100 + x] = own[y * 100 + x];
    scaled[(y + 1) * 100 + x] = own[(y + 1) * 100 + x];
    scaled[(y + 2) * 100 + x] = own[(y + 2) * 100 + x];
    OBL_CHECK(own == scaled); // nothing else changed
}

namespace {

struct FakeBolt {
    std::string log;
    int32_t width = 800, height = 600;
    bool clips = true;
    void WorldToScreen(const float* world, int32_t* out)
    {
        out[0] = static_cast<int32_t>(world[0]);
        out[1] = static_cast<int32_t>(world[1]);
        log += "world;";
    }
    int32_t RendererWidth() const { return width; }
    int32_t RendererHeight() const { return height; }
    void SetMoving(bool on) { log += on ? "moving;" : "still;"; }
    bool ClipRect(int32_t* x, int32_t* y, int32_t* left, int32_t* top, int32_t* right, int32_t* bottom)
    {
        log += "clip " + std::to_string(*x) + "," + std::to_string(*y) + "," + std::to_string(*left) + "," + std::to_string(*top) + "," +
                std::to_string(*right) + "," + std::to_string(*bottom) + ";";
        return clips;
    }
    void BlitMaxBytes(int32_t x, int32_t y, int32_t w, int32_t h, int32_t l, int32_t t, int32_t r, int32_t b)
    {
        log += "blit " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," + std::to_string(h) + "," +
                std::to_string(l) + "," + std::to_string(t) + "," + std::to_string(r) + "," + std::to_string(b) + ";";
    }
    void MarkLightsDirty() { log += "dirty;"; }
};

} // namespace

OBL_TEST_CASE("weather_draw.lightning_without_a_bitmap_does_nothing")
{
    weather::Weather weather{};
    FakeBolt env;
    DrawLightning(weather, env);
    OBL_CHECK_EQ(env.log, std::string());
}

OBL_TEST_CASE("weather_draw.lightning_clips_blits_and_marks_the_lights_dirty")
{
    auto weather = std::make_unique<weather::Weather>();
    char bitmap = 0;
    weather->m_lightningLightmap = &bitmap;
    weather->m_strikePoint[0] = 400;
    weather->m_strikePoint[1] = 700;
    FakeBolt env;
    DrawLightning(*weather, env);
    OBL_CHECK_EQ(env.log, std::string("world;moving;clip 300,100,0,0,200,600;blit 300,100,200,600,0,0,200,600;still;dirty;"));
}

OBL_TEST_CASE("weather_draw.lightning_off_screen_still_marks_the_lights_dirty")
{
    auto weather = std::make_unique<weather::Weather>();
    char bitmap = 0;
    weather->m_lightningLightmap = &bitmap;
    FakeBolt env;
    env.clips = false;
    DrawLightning(*weather, env);
    OBL_CHECK_EQ(env.log, std::string("world;moving;clip -100,-600,0,0,200,600;still;dirty;"));
}
