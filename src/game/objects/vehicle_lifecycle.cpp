#include "game/ui/sprite_bank.h"
#include "game/objects/object_base.h"
#include "game/objects/object_moving.h"
#include "game/ui/loop_sound_control.h"
#include "game/world/world_receive.h"
#include "game/ui/sprite.h"
#include "game/objects/object_vehicle.h"

#include "game/game/game_accessors.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_create_params.h"
#include "core/call_counter.h"
#include "game/objects/vehicle_lifecycle.h"
#include "game/objects/object_base.h"

#include <cstring>
#include "game/world/world_params.h"

// Construction and teardown of vehicles (the logic is vehicle_lifecycle.h).
namespace obl::object {
using audio::CurrentLoopSounds;
using game::GetGrid;
using game::GetWorld;
using game::GetWorldParams;

namespace {
using map::Grid;
using map::GridCell;
using messages::Message;
using object::Vehicle;
using tables::VehicleDef;
using ui::SpriteInstance;
namespace life = vehicle::lifecycle;

test::CallCounter g_construct("objects.vehicle-construct"), g_destroy("objects.vehicle-destroy");

void* SpriteBank()
{
    return ui::GetSpriteBank();
}

// The game side of the Env of vehicle_lifecycle.h.
class LifeEnv {
public:
    const VehicleDef* Definition(const char* key)
    {
        tables::WorldParams* params = GetWorldParams();
        const int id = world::world_params::GetVehicleId(params, key);
        return world::world_params::GetVehicleDefinition(params, id);
    }
    // The rest of the message and of the payload stays zero.
    void PlaceTowed(int32_t owner, const char* key)
    {
        Message message{};
        message.sender = owner;
        message.receiver = -2;
        message.data = 4;
        object::VehicleCreateParams payload{};
        std::strncpy(payload.m_key, key, sizeof(payload.m_key) - 1);
        payload.m_homeId = owner;
        world::PostPlaceCommand(message, &payload);
    }
    void* CreateSprite(const char* name) { return (static_cast<ui::SpriteBank*>(SpriteBank()))->CreateObservedInstance(name); }
    void StartAnim(void* sprite, const char* anim, float startTime, const float* world)
    {
        (static_cast<SpriteInstance*>(sprite))->SetAnimByName(anim, startTime, const_cast<float*>(world));
    }
    float WorldTime() { return GetWorld()->m_gameTime; }
    int32_t PaletteCount(void* sprite) { return static_cast<SpriteInstance*>(sprite)->m_sprite->m_paletteCount; }
    void SetPaletteIndex(void* sprite, int32_t index) { static_cast<SpriteInstance*>(sprite)->m_paletteIndex = index; }

    void NeighbourCells(uint32_t x, uint32_t y, GridCell* out[8]) { GetGrid()->GetNeighbourCells(x, y, out); }
    GridCell* CellAt(uint32_t x, uint32_t y)
    {
        Grid* grid = GetGrid();
        return &grid->m_cells[grid->TileIndex(x, y)];
    }
    // The object's position converted through the grid.
    void ObjectTile(const Vehicle& object, uint32_t out[2])
    {
        float position[3];
        object.GetPosition(position);
        auto* grid = GetGrid();
        const grid_math::TileXY tile = grid->WorldPointToTile(position[0], position[1]);
        out[0] = tile.x;
        out[1] = tile.y;
    }
    void StopLoop(int32_t id) { CurrentLoopSounds()->StopLoop(id); }
    void ReleaseSprite(void* sprite) { static_cast<ui::SpriteBank*>(SpriteBank())->ReleaseInstance(sprite); }
};

} // namespace

Vehicle::Vehicle(const char* name) : Mover(name)
{
    g_construct.Count();
    LifeEnv env;
    vehicle::lifecycle::Construct(*this, name, env);
}

Vehicle::~Vehicle()
{
    g_destroy.Count();
    LifeEnv env;
    vehicle::lifecycle::Destruct(*this, env);
}
} // namespace obl::object
