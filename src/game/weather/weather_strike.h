#pragma once

#include "game/weather/weather_state.h"

#include "core/float_to_int.h"
#include "game/weather/weather_control.h"

#include <cstdint>
#include <cstring>

namespace obl::weather {

namespace strike {

inline constexpr uint32_t kBoltRowBytes = 200; // one bitmap row: 200 columns of one byte
inline constexpr uint8_t kBoltPixel = 0x11; // The lightning draw brightens where the byte is set
inline constexpr int32_t kBoltStartColumn = 100; // the bolt starts in the middle
inline constexpr int32_t kBoltMinColumn = 50; // segment targets: 50 + rand % 100 scaled by 0.5..1 down the bolt
inline constexpr int32_t kBoltSegmentMin = 10; // a segment is 10 + rand % 50 rows long
inline constexpr float kBoltHalf = 0.5f;
inline constexpr float kBoltWidthScale = 15.0f; // the bolt is 30 pixels wide at the bottom row
inline constexpr int32_t kDriftLimit = 15; // g_LightningBoltDrift stays in -15..15

inline constexpr int32_t kNearRadius = 4; // The object manager's near-tile query radius around the strike tile
inline constexpr int32_t kVandalRange = 4; // scenery closer than 4 tiles on both axes is vandalised
inline constexpr int kStateHeadingDesire = 13; // STATE_GUEST_HEADING_DESIRE: the guests that get knocked out
inline constexpr int kStateKnockedOut = 33; // STATE_GUEST_KNOCKED_OUT (0x21), the state change sent to them
inline constexpr int32_t kKnockedOutAction = 0xb; // Guest::m_ActionState of a knocked-out guest
inline constexpr int kStateOccupied = 4; // STATE_OCCUPIED: scenery in use is spared
inline constexpr int kStateChangeMessage = 3, kMoveStopMessage = 21;
inline constexpr int kSender = -2; // the world as the sender of the messages
inline constexpr int kDirectionRow = 0; // Grid::GetNeighbour direction along a footprint row
inline constexpr int kDirectionNextRow = 6; // and to the next row
inline constexpr float kThunderVolume = 0.8f;
inline constexpr uint8_t kThunderFlags = 1;
inline constexpr char kThunderclap[] = "thunderclap";

} // namespace strike

struct StrikeTile {
    int32_t x, y;
};

template<class Env> void CreateLightningBolt(weather::Weather& weather, int32_t height, Env& env)
{
    using namespace strike;
    const uint32_t bytes = static_cast<uint32_t>(height) * kBoltRowBytes;
    weather.m_lightningLightmap = env.NewBitmap(bytes);
    std::memset(weather.m_lightningLightmap, 0, bytes);
    if (height <= 0) return;
    const double rows = static_cast<float>(height);
    const double half = static_cast<double>(kBoltHalf);
    int32_t row = 0, column = kBoltStartColumn;
    for (;;) {
        const int32_t pick = env.Rand() % 100;
        const int32_t target = FloatToIntWrap(static_cast<double>(pick) * (static_cast<double>(row) / rows * half + half)) + kBoltMinColumn;
        const int32_t length = env.Rand() % 50 + kBoltSegmentMin;
        const int32_t start = row, startColumn = column, end = row + length;
        const float slope = static_cast<float>(static_cast<double>(target - startColumn) / static_cast<double>(length));
        if (row >= height) return;
        while (row < end) {
            ++row;
            column = FloatToIntWrap(static_cast<double>(row - start) * static_cast<double>(slope)) + startColumn;
            const double rowValue = static_cast<float>(row);
            const int32_t wobble = env.Rand() % 5 - 2;
            int32_t drift = env.Drift() + FloatToIntWrap(static_cast<double>(wobble) * (rowValue / rows * half + half));
            if (drift > kDriftLimit)
                drift = kDriftLimit;
            else if (drift < -kDriftLimit)
                drift = -kDriftLimit;
            env.Drift() = drift;
            const uint32_t width = static_cast<uint32_t>(FloatToIntWrap(rowValue / (rows * half) * static_cast<double>(kBoltWidthScale)));
            const int32_t offset = static_cast<int32_t>(
                    static_cast<uint32_t>(drift) + static_cast<uint32_t>(height - row) * kBoltRowBytes + static_cast<uint32_t>(column));
            std::memset(static_cast<uint8_t*>(weather.m_lightningLightmap) + offset, kBoltPixel, width);
            if (row >= height) return;
        }
    }
}

namespace lighting {

inline int32_t TileDistance(int32_t a, int32_t b)
{
    const uint32_t difference = static_cast<uint32_t>(a) - static_cast<uint32_t>(b);
    const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(difference) >> 31);
    return static_cast<int32_t>((difference ^ sign) - sign);
}

inline bool StrikeDividesByZero(const weather::Weather& weather, bool targetFound)
{
    if (weather.m_hasStrikeTarget && targetFound) return false;
    return weather.m_strikeArea.right == weather.m_strikeArea.left || weather.m_strikeArea.bottom == weather.m_strikeArea.top;
}

} // namespace lighting

template<class Env> void Strike(weather::Weather& weather, float now, Env& env)
{
    using namespace strike;
    const float* target = weather.m_hasStrikeTarget ? env.ObjectPoint(weather.m_strikeBuildingId) : nullptr;
    if (target) {
        std::memcpy(weather.m_strikePoint, target, sizeof(weather.m_strikePoint));
        env.DamageTarget(weather.m_strikeBuildingId);
    } else {
        const Rect& area = weather.m_strikeArea;
        const int32_t left = area.left;
        int32_t value = env.Rand() % static_cast<int32_t>(static_cast<uint32_t>(area.right) - static_cast<uint32_t>(left));
        weather.m_strikePoint[0] = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(value) + static_cast<uint32_t>(left)));
        const int32_t top = area.top;
        value = env.Rand() % static_cast<int32_t>(static_cast<uint32_t>(area.bottom) - static_cast<uint32_t>(top));
        weather.m_strikePoint[1] = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(value) + static_cast<uint32_t>(top)));
    }
    env.CreateBolt();

    decltype(env.Building(0)) const* near = nullptr;
    const uint32_t nearCount = env.NearObjects(env.PointToTile(weather.m_strikePoint), kNearRadius, &near);
    for (uint32_t i = 0; i < nearCount; ++i) {
        auto* object = near[i];
        if (object->m_type != decltype(object->m_type)::Guest || env.ObjectState(object) != kStateHeadingDesire) continue;
        env.SetActionState(object, kKnockedOutAction);
        typename Env::Message state{kSender, static_cast<int>(near[i]->m_id), kStateChangeMessage, 0, kStateKnockedOut, 0, 0.0f, 0.0f};
        env.Post(state);
        typename Env::Message stop{kSender, static_cast<int>(near[i]->m_id), kMoveStopMessage, 0, 0, 0, 0.0f, 0.0f};
        env.Post(stop);
    }

    const uint32_t buildings = env.BuildingCount();
    const StrikeTile hit = env.PointToTile(weather.m_strikePoint);
    for (uint32_t i = 0; i < buildings; ++i) {
        StrikeTile rowStart = env.TilePosition(env.Building(i));
        for (uint32_t j = 0; j < env.GridWidth(i); ++j) {
            StrikeTile cell = rowStart;
            for (uint32_t k = 0; k < env.GridHeight(i); ++k) {
                cell = env.Neighbour(cell, kDirectionRow);
                if (cell.x != hit.x || cell.y != hit.y) continue;
                env.DamageBuilding(env.Building(i));
                j = env.GridWidth(i);
                k = env.GridHeight(i);
                i = buildings - 1;
            }
            rowStart = env.Neighbour(rowStart, kDirectionNextRow);
        }
    }

    const uint32_t others = env.OtherCount();
    for (uint32_t i = 0; i < others; ++i) {
        auto* object = env.Other(i);
        if (object->m_type != decltype(object->m_type)::Scenery || !env.Repairable(object) || env.ObjectState(object) == kStateOccupied)
            continue;
        const StrikeTile at = env.TilePosition(object);
        if (lighting::TileDistance(at.x, hit.x) < kVandalRange && lighting::TileDistance(at.y, hit.y) < kVandalRange) env.Vandalise(object);
    }

    weather.m_hasStrikeTarget = 0;
    weather.m_strikes = 0;
    const int channel = env.FindSound(kThunderclap);
    if (channel != -1)
        env.StartSound(channel);
    else
        env.SetSoundVolume(env.PlaySound(kThunderclap), kThunderVolume);
    weather.m_flashTime = now;
    weather.m_lightFlash = 1;
    weather.m_flashColor = 0;
    if (weather.m_strikeLevel != 0) {
        weather.m_strikes = 1;
        weather.m_nextStrikeTime = NextStrikeTime(weather.m_strikeLevel, now);
    }
}

} // namespace obl::weather
