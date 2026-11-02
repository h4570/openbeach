#include "game/ui/renderer_api.h"
#include "game/ui/day_night_present_runtime.h"
#include "game/game/settings_store.h"

#include "game/game/game_accessors.h"
#include "game/weather/weather_state.h"
#include "game/ui/day_night_present.h"
#include "game/game/game.h"
#include "game/ui/renderer_types.h"
#include "game/world/world_state.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/ui/render_geometry.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// What surrounds the pixel loops of src/game/ui/day_night_present: back-buffer lock/unlock, the shimmer table pick and the light buffer
// clean-up. The loops are one function over the buffers.
namespace obl::ui {
namespace {

using ui::Renderer;

// Presents counted for `call-report ui.day-night-present`.
test::CallCounter g_presents("ui.day-night-present");

// What the dispatcher and each loop read.
struct Plan {
    day_night::Variant variant;
    day_night::Frame frame;
};

// One ulp below 15, so a time of exactly n / 15 hours still falls into the previous step.
constexpr float kShimmerRate = 14.999999f;

// Heat-wave shimmer: trunc(game time * 15) % 6 picks one of the six haze images (Renderer::m_hazeImages), whose pixels are the 0x300-byte offset table.
// The signed remainder keeps the sign of the dividend; a negative index (negative game time) reads before the pointer array.
const uint8_t* ShimmerTable(const Renderer* renderer)
{
    const float time = game::GetWorld()->m_gameTime;
    const int index = render_geometry::FloatToIntWrap(static_cast<double>(time) * static_cast<double>(kShimmerRate)) % 6;
    return renderer->m_hazeImages[index]->m_pixels;
}

Plan MakePlan(const Renderer* renderer)
{
    Plan plan{};
    const uint32_t weather = game::GetWeather()->m_currentState;
    plan.variant = day_night::ChooseVariant(weather, game::settings_store::Current().m_lightEffects != 0);
    day_night::Frame& f = plan.frame;
    f.width = renderer->m_width;
    f.height = renderer->m_height;
    f.bottomTrim = renderer->m_bottomTrim;
    f.rgb555 = renderer->m_rgb555 == 1;
    f.doubled = renderer->m_zoomLevel == 0;
    f.scene = renderer->m_workBuffer;
    f.light = renderer->m_lightBuffer;
    f.tones = &game::GetWeather()->m_toneTables[0][0];
    f.shimmer = plan.variant == day_night::Variant::NormalWeather ? ShimmerTable(renderer) : nullptr;
    return plan;
}

// The record of the current surface (Renderer::m_activeSurface).
ui::RendererSurface* CurrentSurface(const Renderer* renderer)
{
    return ui::SurfaceAt(*renderer, renderer->m_activeSurface);
}

uint32_t SurfacePitch(const ui::RendererSurface* surface)
{
    return surface->m_pitch;
}

// The clean-up the three loops that use the light buffer end with: every pixel's level back to 0, the flags the lighting code reads set.
void ClearLightBuffer(Renderer* renderer)
{
    renderer->m_lightsDirtyFrom = 0;
    renderer->m_lightsDirty = 1;
    std::memset(renderer->m_lightBuffer, 0, static_cast<size_t>(renderer->m_height) * static_cast<size_t>(renderer->m_width));
}

void Present(Renderer* renderer)
{
    const Plan plan = MakePlan(renderer);
    renderer->m_activeSurface = 0;
    if (auto* bits = obl::ui::renderer::GetBackBufferBits(renderer)) {
        const auto* surface = CurrentSurface(renderer);
        day_night::Present(plan.variant, plan.frame, reinterpret_cast<uint8_t*>(bits), SurfacePitch(surface));
        renderer->m_surfaceLocked = 0;
    }
    if (day_night::ClearsLightBuffer(plan.variant, plan.frame.doubled)) ClearLightBuffer(renderer);
}

} // namespace

void PresentDayNight(Renderer* renderer)
{
    g_presents.Count();
    Present(renderer);
}

} // namespace obl::ui
