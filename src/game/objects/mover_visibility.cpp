#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/map_view_access.h"
#include "game/objects/mover_visibility.h"
#include "game/objects/object_base.h"
#include "game/objects/object_moving.h"

#include <cstdint>
#include "game/ui/map_view_math.h"

// The one moving-object query that reads the screen (mover_visibility.h has the logic and the contract of the Env).
namespace obl::object {

namespace {

struct ScreenEnv {
    const Base* Building(int id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    void WorldToScreen(const float* world, int out[2]) { ui::GetMapView()->ProjectWorldPoint(out, world); }
    uint32_t Depth(const float* world) { return ui::GetMapView()->GetDepth(world); }
    void TilePosition(const Mover& object, uint32_t out[2]) { object.GetTilePosition(reinterpret_cast<int*>(out)); }
    int ZoomLevel() { return game::GetRenderer()->m_zoomLevel; }
    int ViewWidth() { return game::GetRenderer()->m_view[2]; }
    int ViewHeight() { return game::GetRenderer()->m_view[3]; }
    int BufferStride() { return game::GetRenderer()->m_width; }
    uint32_t BufferWord(int index) { return game::GetRenderer()->m_workBuffer[index]; }
    uint32_t GridWidth() { return game::GetGrid()->m_width; }
    uint32_t GridHeight() { return game::GetGrid()->m_height; }
    uint16_t ZOffset(uint32_t cell) { return game::GetGrid()->m_cells[cell].m_zOffset; }
};

} // namespace

bool Mover::IsVisibleAgainstBuilding(int buildingId)
{
    ScreenEnv env;
    return moving::IsVisibleAgainstBuilding(*this, buildingId, env);
}
} // namespace obl::object
