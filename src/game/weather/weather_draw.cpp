#include "game/ui/renderer_api.h"
#include "game/ui/blit.h"
#include "game/weather/weather_state.h"
#include "game/weather/weather_draw.h"

#include "game/game/game_accessors.h"
#include "game/ui/map_view_access.h"
#include "game/weather/weather.h"
#include "game/ui/map_view.h"
#include "game/ui/renderer_types.h"
#include "game/world/world_state.h"
#include "core/call_counter.h"
#include "game/weather/weather_draw.h"

#include <cstdint>

namespace obl::weather {
namespace {

using ui::Renderer;
using weather::RainDropBuffer;
using weather::Weather;

test::CallCounter g_answeredRain("weather.draw-rain");

RainDropBuffer& Drops()
{
    return runtime::RainDrops();
}

// ---- the rain draw --------------------------------------------------------------------------------------------------------------

weather::RainView CurrentRainView(const Weather* weather)
{
    const Renderer* renderer = game::GetRenderer();
    const ui::MapView* map = ui::GetMapView();
    const world_data::World* world = game::GetWorld();
    return {static_cast<int32_t>(weather->m_currentState), map->m_viewWidth, map->m_viewHeight, renderer->m_width, renderer->m_rgb555 == 1,
            world->m_paused != 0};
}

// ---- the lightning draw ---------------------------------------------------------------------------------------------------------

// The game's functions behind the Env of weather::DrawLightning.
class GameBoltEnv {
public:
    GameBoltEnv() : m_renderer(game::GetRenderer()) {}
    void WorldToScreen(const float* world, int32_t* out) { ui::GetMapView()->ProjectWorldPoint(out, world); }
    int32_t RendererWidth() const { return m_renderer->m_width; }
    int32_t RendererHeight() const { return m_renderer->m_height; }
    void SetMoving(bool moving) { m_renderer->m_directMode = moving ? 1 : 0; }
    bool ClipRect(int32_t* x, int32_t* y, int32_t* left, int32_t* top, int32_t* right, int32_t* bottom)
    {
        int* p[6] = {x, y, left, top, right, bottom};
        return obl::ui::renderer::ClipRect(m_renderer, p[0], p[1], p[2], p[3], p[4], p[5]) != 0;
    }
    void BlitMaxBytes(int32_t x, int32_t y, int32_t width, int32_t height, int32_t left, int32_t top, int32_t right, int32_t bottom)
    {
        (ui::pixel_primitives::MaxBytes8)(m_renderer->m_lightBuffer, static_cast<const uint8_t*>(m_pending), 0, m_renderer->m_width, x, y,
                width, height, left, top, right, bottom);
    }
    void MarkLightsDirty()
    {
        m_renderer->m_lightsDirty = 1;
        m_renderer->m_lightsDirtyFrom = 0;
    }
    const void* m_pending = nullptr; // the bolt bitmap the blit reads (Weather::m_lightningLightmap)

private:
    Renderer* m_renderer;
};

void DrawBolt(Weather* weather)
{
    GameBoltEnv env;
    env.m_pending = weather->m_lightningLightmap;
    weather::DrawLightning(*weather, env);
}

} // namespace

namespace runtime {

weather::RainDropBuffer& RainDrops()
{
    static weather::RainDropBuffer g_rainDrops;
    return g_rainDrops;
}

void DrawRain(weather::Weather* weatherState)
{
    g_answeredRain.Count();
    const weather::RainView view = CurrentRainView(weatherState);
    weather::DrawRain(
            view, game::GetRenderer()->m_workBuffer, Drops(), weather::RainDropCountScaled(view.state, view.viewWidth, view.viewHeight));
}

} // namespace runtime

void DrawLightningFrame(Weather* weather)
{
    DrawBolt(weather);
}

} // namespace obl::weather
