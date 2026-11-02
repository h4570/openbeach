#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "core/rect.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// SetPosition updates scenery placement; RequestRemoval restores the background and removes its effects.
// Src/game/objects/scenery.cpp supplies the environment; unit tests use a fake.
//   SetPosition:
// void* CellAt(const uint32_t xyz[3]) the cell at the world position on the grid (the float bits go through unchanged)
// bool CellHas(void* cell, Scenery&) tests whether the object belongs to the cell's list
// void RemoveFromCell(void* cell, Scenery&) removing the object from the cell
// void AddToCell(void* cell, Scenery&) appending the object to the cell (push_back)
// double TerrainHeight(int worldX, int worldY) the terrain height on g_MapView
//   RequestRemoval:
// void RestoreBackground(Rect&) restoring the background rectangle
// void RebuildPathTiles(Rect&) rebuilding path tiles in the rectangle
//     uint32_t NumBuildings()  Building* Building(i)    the object manager's building list (the slot is read when the loop reaches it)
//     bool HasDemolishJob(Building&)  bool HasBuildJob(Building&)
// const Rect* BuildingRect(Building&) the building's UpdateScreenRect (recomputes and returns the building's rect)
// bool Intersect(const Rect& a, const Rect& b, Rect& out) rectangle intersection (out written only on overlap)
// void BakeBuilding(Building&, const Rect& clip, const float pos[3], int gridW, int gridH) baking the building sprite into the background
// void Post(const messages::Message&) posts to g_MessageHandler
namespace obl::scenery {

using object::Scenery;
using screen::Rect;

// The cells hold raw float bits: passing a float by value could quiet a signalling NaN.
inline uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The object moves between the object lists of two grid cells only when the cell under the NEW position does not list it yet: then it leaves the
// cell under its CURRENT position and joins the new one. (An object that was never in the old cell is removed from it all the same: the removal
// walks the list and finds nothing.) The position is stored as given, and z is then replaced by the terrain height under (x, y), truncated like
// truncated toward zero (y is converted first).
template<class Env> void SetPosition(Env& env, Scenery& scenery, const float* position)
{
    const uint32_t target[3] = {FloatBits(position[0]), FloatBits(position[1]), FloatBits(position[2])};
    void* newCell = env.CellAt(target);
    if (!env.CellHas(newCell, scenery)) {
        const uint32_t current[3] = {FloatBits(scenery.m_WorldXYZ[0]), FloatBits(scenery.m_WorldXYZ[1]), FloatBits(scenery.m_WorldXYZ[2])};
        env.RemoveFromCell(env.CellAt(current), scenery);
        env.AddToCell(newCell, scenery);
    }
    scenery.m_WorldXYZ[0] = position[0];
    scenery.m_WorldXYZ[1] = position[1];
    scenery.m_WorldXYZ[2] = position[2];
    const int worldY = FloatToIntWrap(static_cast<double>(position[1]));
    const int worldX = FloatToIntWrap(static_cast<double>(position[0]));
    scenery.m_WorldXYZ[2] = static_cast<float>(env.TerrainHeight(worldX, worldY));
}

// The side of the map blocks: the scenery's screen rectangle is limited to 0..0x1000 (the lower bounds and the upper ones separately).
constexpr int kMapPixels = 0x1000;
constexpr int kRemovalSender = -3; // the "dozer" sender id of the delete message

// What the removal posts: MT_STATECHANGE (3) with data 3 from sender -3 to the scenery. The last three words (`extra`, `floatData`, `time`) are
// zero.
inline messages::Message RemovalMessage(uint32_t receiver)
{
    messages::Message message{};
    message.sender = kRemovalSender;
    message.receiver = static_cast<int>(receiver);
    message.type = messages::MT_STATECHANGE;
    message.flags = 0;
    message.data = 3;
    return message;
}

// A decoration burned into the map background (`SceneryDef::m_burn`) first gets its rectangle restored from the MapCache and the paths under it
// rebuilt, and every building that overlaps it (and is not being built or demolished) is baked over it again, clipped to the rectangle; the
// object's own rectangle is zeroed. Every kind then posts the delete message.
template<class Env> void RequestRemoval(Env& env, Scenery& scenery)
{
    if (scenery.m_definition->m_burn != 0) {
        Rect& screen = scenery.m_ScrXYRect;
        if (screen.top < 0) screen.top = 0;
        if (screen.bottom > kMapPixels) screen.bottom = kMapPixels;
        if (screen.left < 0) screen.left = 0;
        if (screen.right > kMapPixels) screen.right = kMapPixels;
        env.RestoreBackground(screen);
        Rect area{screen.left, screen.top, screen.right, screen.bottom};
        screen = Rect{0, 0, 0, 0};
        env.RebuildPathTiles(area);

        const uint32_t count = env.NumBuildings();
        for (uint32_t i = 0; i < count; ++i) {
            object::Building* building = env.Building(i);
            if (env.HasDemolishJob(*building) || env.HasBuildJob(*building)) continue;
            Rect clipped;
            if (!env.Intersect(area, *env.BuildingRect(*building), clipped)) continue;
            float position[3];
            std::memcpy(position, building->m_WorldXYZ, sizeof(position));
            env.BakeBuilding(*building, clipped, position, building->m_definition->m_GridWidth, building->m_definition->m_GridHeight);
        }
    }
    env.Post(RemovalMessage(scenery.m_id));
}

} // namespace obl::scenery
