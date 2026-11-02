
#include "game/game/game_accessors.h"
#include "game/objects/object_base.h"
#include "game/map/cell_objects.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_scenery.h"
#include "game/objects/scenery_record.h"
#include "core/call_counter.h"
#include "game/objects/scenery_draw.h"
#include "game/objects/scenery_lifecycle.h"

#include <cstdint>
#include <cstring>

// The game side of the obl.save load steps of a scenery object (src/game/objects/scenery_record.h): the draw routine and the footprint.
namespace obl::object {
using game::GetGrid;

namespace {

using object::Scenery;
map::Grid& TheGrid()
{
    return *GetGrid();
}

struct PlaceEnv {
    Scenery::DrawFunc DrawFor(scenery_record::Pick pick)
    {
        using scenery_record::Pick;
        switch (pick) {
        case Pick::Preview: return &SceneryDrawPreview;
        case Pick::Bench: return &SceneryDrawBench;
        case Pick::Lounger: return &SceneryDrawLounger;
        case Pick::Burned: return &SceneryDrawBurned;
        case Pick::Generic: break;
        }
        return &SceneryDrawGeneric;
    }
    void TilePosition(Scenery& s, uint32_t& x, uint32_t& y)
    {
        int tile[2];
        s.GetTilePosition(tile);
        x = static_cast<uint32_t>(tile[0]);
        y = static_cast<uint32_t>(tile[1]);
    }
    void AddToCell(uint32_t x, uint32_t y, Scenery& s) { cell_objects::AddObject(TheGrid().Cell(x, y), s); }
    void Neighbour(uint32_t& x, uint32_t& y, int direction)
    {
        const grid_math::TileXY next = TheGrid().GetNeighbour(x, y, direction);
        x = next.x;
        y = next.y;
    }
};

} // namespace

void ScenerySetDraw(Scenery& scenery)
{
    PlaceEnv env;
    scenery_record::SetDraw(scenery, env);
}

void SceneryJoinFootprint(Scenery& scenery)
{
    PlaceEnv env;
    scenery_record::JoinFootprint(scenery, env);
}

} // namespace obl::object

namespace obl::object {} // namespace obl::object
