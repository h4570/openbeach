#pragma once

#include "game/ui/map_view.h"
#include "core/rect.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

namespace obl::map_rebuild {

inline constexpr double kPeriod = 1.0;

inline constexpr float kClockFloor = 0.5f;

inline int32_t QueueSize(const ui::MapView& view)
{
    return static_cast<int32_t>(view.m_burnQueue.size());
}

template<class Env> void QueueBlockRebuild(Env& env, ui::MapView& view, int blockX, int blockY, int width, int height)
{
    const auto clip = [](int64_t value, int32_t limit) { return static_cast<int>(value < 0 ? 0 : value > limit ? limit : value); };
    const int beginY = clip(blockY, view.m_blocksPerColumn);
    const int beginX = clip(blockX, view.m_blocksPerRow);
    const int endY = clip(static_cast<int64_t>(blockY) + height, view.m_blocksPerColumn);
    const int endX = clip(static_cast<int64_t>(blockX) + width, view.m_blocksPerRow);
    for (int y = beginY; y < endY; ++y)
        for (int x = beginX; x < endX; ++x) view.m_burnQueue.push_back(y * view.m_blocksPerRow + x);
}

template<class Env> void RebuildBlock(Env& env, ui::MapView& view, int32_t index)
{
    const int blockX = index % view.m_blocksPerRow;
    const int blockY = index / view.m_blocksPerRow;
    env.LoadBlockPair(blockX, blockY);

    const int size = view.m_blockSize;
    screen::Rect block{size * blockX, size * blockY, size + size * blockX, size * blockY + size};
    env.RebuildPathTiles(block);

    const uint32_t count = env.NumBuildings();
    for (uint32_t i = 0; i < count; ++i) {
        object::Building* building = env.Building(i);
        screen::Rect clipped;
        if (!env.Intersect(block, *env.BuildingRect(*building), clipped)) continue;
        if (env.HasDemolishJob(*building) || env.HasBuildJob(*building)) continue;
        float position[3];
        std::memcpy(position, building->m_WorldXYZ, sizeof(position));
        env.BakeBuilding(*building, clipped, position, building->m_definition->m_GridWidth, building->m_definition->m_GridHeight);
    }
}

template<class Env> void ProcessRebuildQueue(Env& env, ui::MapView& view, float time)
{
    if (!(view.m_lastUnBurnTime >= kClockFloor)) view.m_lastUnBurnTime = static_cast<float>(static_cast<double>(time) - kPeriod);
    double ahead = static_cast<double>(time) - static_cast<double>(view.m_lastUnBurnTime);
    while (ahead > kPeriod) {
        if (!view.m_burnQueue.empty()) {
            RebuildBlock(env, view, view.m_burnQueue.front());
            view.m_burnQueue.pop_front();
        }
        const double clock = static_cast<double>(view.m_lastUnBurnTime) + kPeriod;
        view.m_lastUnBurnTime = static_cast<float>(clock);
        ahead = static_cast<double>(time) - clock;
    }
}

} // namespace obl::map_rebuild
