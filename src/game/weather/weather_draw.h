#pragma once

#include "game/weather/weather_state.h"

#include <algorithm>
#include <cstdint>

// The two weather draws that run inside the frame (docs/specs/weather.md): the rain draw and
// The lightning draw. Both write the renderer's buffers, which the caller passes in; nothing here touches the game.
namespace obl::weather {

// ---- the rain draw ------------------------------------------------------------------------------------------------------------------

inline constexpr int32_t kRainLight = 3, kRainHeavy = 4, kRainStorm = 5; // Weather::m_currentState values that rain
inline constexpr int32_t kRainReferenceArea = 800 * 600;
inline constexpr uint32_t kMaxRainDrops = 8 * weather::kRainDropCount; // cap of the scaled density (4K at heavy rain)

inline bool IsRaining(int32_t state)
{
    return state == kRainLight || state == kRainHeavy || state == kRainStorm;
}

inline uint32_t RainDropCountBase(int32_t state, int32_t viewHeight)
{
    uint32_t count = state == kRainLight ? 2000 : 4000;
    return viewHeight > 600 ? count * 2 : count;
}

inline uint32_t RainDropCountScaled(int32_t state, int32_t viewWidth, int32_t viewHeight)
{
    const uint32_t floor = RainDropCountBase(state, viewHeight);
    if (viewWidth <= 0 || viewHeight <= 0) return floor;
    const uint64_t area = static_cast<uint64_t>(viewWidth) * static_cast<uint64_t>(viewHeight);
    const uint64_t base = state == kRainLight ? 2000 : 4000;
    const uint64_t scaled = base * area / kRainReferenceArea;
    return static_cast<uint32_t>(std::min<uint64_t>(std::max<uint64_t>(scaled, floor), kMaxRainDrops));
}

struct RainView {
    int32_t state; // Weather::m_currentState
    int32_t viewWidth; // MapView::m_viewWidth, the x range of a drop
    int32_t viewHeight; // MapView::m_viewHeight, the y range is two rows shorter (a drop is two or three rows tall)
    int32_t pitch; // Renderer::m_width: words per row of the work buffer
    bool rgb555; // Renderer::m_rgb555
    bool paused; // World::m_paused: the drops stand still
};

// Halves the 16-bit colour of a work-buffer word (low half; the high half is the depth and stays).
inline uint32_t DimWord(uint32_t word, bool rgb555)
{
    const uint32_t colour = word & 0xffff;
    const uint32_t dimmed = rgb555 ? (colour >> 1 & 0x3def) + 0x3def : (colour >> 1 & 0x7bef) + 0x7bef;
    return (word & 0xffff0000) + dimmed;
}

inline void DimDrop(uint32_t* word, int32_t pitch, bool rgb555, bool tall)
{
    word[0] = DimWord(word[0], rgb555);
    word[pitch] = DimWord(word[pitch], rgb555);
    if (tall) word[2 * static_cast<intptr_t>(pitch)] = DimWord(word[2 * static_cast<intptr_t>(pitch)], rgb555);
}

inline constexpr int32_t kLapStrideX = 337, kLapStrideY = 211;

inline void DrawRain(const RainView& view, uint32_t* work, weather::RainDropBuffer& drops, uint32_t count)
{
    if (!IsRaining(view.state) || view.viewWidth <= 0 || view.viewHeight - 2 <= 0 || !work) return;
    const bool light = view.state == kRainLight;
    const int32_t rows = view.viewHeight - 2;
    const uint32_t own = std::min(count, weather::kRainDropCount);
    for (uint32_t i = 0; i < own; ++i) {
        int32_t x = static_cast<int32_t>(drops.m_xy[i][0]);
        int32_t y = static_cast<int32_t>(drops.m_xy[i][1]);
        if (!view.paused) {
            x = (x + (light ? 1 : 2)) % view.viewWidth;
            y = (y + (light ? 8 : 0xe)) % rows;
            drops.m_xy[i][0] = static_cast<uint32_t>(x);
            drops.m_xy[i][1] = static_cast<uint32_t>(y);
        }
        DimDrop(work + static_cast<intptr_t>(y) * view.pitch + x, view.pitch, view.rgb555, !light);
    }
    for (uint32_t i = weather::kRainDropCount; i < count; ++i) {
        const uint32_t lap = i / weather::kRainDropCount;
        const auto& base = drops.m_xy[i % weather::kRainDropCount];
        const int64_t x = (static_cast<int64_t>(static_cast<int32_t>(base[0])) + static_cast<int64_t>(lap) * kLapStrideX) % view.viewWidth;
        const int64_t y = (static_cast<int64_t>(static_cast<int32_t>(base[1])) + static_cast<int64_t>(lap) * kLapStrideY) % rows;
        DimDrop(work + y * view.pitch + x, view.pitch, view.rgb555, !light);
    }
}

// ---- the lightning draw -------------------------------------------------------------------------------------------------------------

inline constexpr int32_t kBoltBitmapWidth = 200;

template<class Env> void DrawLightning(const weather::Weather& weather, Env& env)
{
    if (!weather.m_lightningLightmap) return;
    int32_t at[2];
    env.WorldToScreen(weather.m_strikePoint, at);
    int32_t x = at[0] - 100;
    int32_t y = at[1] - env.RendererHeight();
    int32_t left = 0, top = 0, right = kBoltBitmapWidth, bottom = env.RendererHeight();
    env.SetMoving(true);
    if (env.ClipRect(&x, &y, &left, &top, &right, &bottom))
        env.BlitMaxBytes(x, y, kBoltBitmapWidth, env.RendererHeight(), left, top, right, bottom);
    env.SetMoving(false);
    env.MarkLightsDirty();
}

} // namespace obl::weather

namespace obl::weather {
void DrawLightningFrame(Weather* weather);
}
