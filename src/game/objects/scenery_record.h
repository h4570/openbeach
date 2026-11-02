#pragma once

#include "core/ascii_case.h"
#include "game/ui/sprite.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// The steps of restoring saved scenery around the base object that the obl.save reader (save_objects_load.cpp) replays. The game side
// (src/game/objects/scenery_serial.cpp) supplies the Env:
//   Scenery::DrawFunc DrawFor(Pick)        the draw routine behind a Pick (C++ SceneryDrawPreview/Bench/Lounger/Burned/Generic)
// void TilePosition(object, x&, y&) the object's tile position
// void AddToCell(x, y, object) appending the object to the cell on the grid cell of the tile (coordinates wrap modulo the grid)
// void Neighbour(x&, y&, direction) the neighbouring tile (the output is written back over the input)
namespace obl::scenery_record {

using object::Scenery;

// Loading selects m_draw according to sprite type: a sprite of type 5 (a building preview sprite) is drawn as a preview whatever the name; otherwise
// by name (case-insensitive), then a decoration burned into the background; everything else uses the generic draw.
enum class Pick { Preview, Bench, Lounger, Burned, Generic };

inline Pick PickDraw(const Scenery& s)
{
    const auto* instance = static_cast<const ui::SpriteInstance*>(s.m_sprite);
    if (instance->m_sprite->m_type == 5) return Pick::Preview;
    if (ascii::EqualsNoCase(s.m_Name, "BENCH")) return Pick::Bench;
    if (ascii::EqualsNoCase(s.m_Name, "LOUNGER")) return Pick::Lounger;
    return s.m_definition->m_burn ? Pick::Burned : Pick::Generic;
}

// Load's steps around the base: the draw routine (from the constructed sprite, before the base loads) and the footprint (after it). The obl.save
// reader (save_objects_load.cpp) calls them at the same points.
template<class Env> void SetDraw(Scenery& s, Env& env)
{
    s.m_draw = env.DrawFor(PickDraw(s));
}

// The footprint: every tile of the grid_w x grid_h block that starts at the object's tile, except that tile itself (the object's own cell is
// joined by whoever places it), is told about the object. Columns step with direction 6, rows with direction 0.
template<class Env> void JoinFootprint(Scenery& s, Env& env)
{
    uint32_t tileX, tileY;
    env.TilePosition(s, tileX, tileY);
    uint32_t outerX = tileX, outerY = tileY;
    for (uint32_t i = 0; i < s.m_gridW; ++i) {
        uint32_t x = outerX, y = outerY;
        for (uint32_t j = 0; j < s.m_gridH; ++j) {
            if (x != tileX || y != tileY) env.AddToCell(x, y, s);
            env.Neighbour(x, y, 0);
        }
        env.Neighbour(outerX, outerY, 6);
    }
}

} // namespace obl::scenery_record
