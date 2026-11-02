#include "game/ui/sound_api.h"
#include "game/objects/object_base.h"
#include "game/objects/object_moving.h"
#include "game/ui/loop_sound_control.h"
#include "game/ui/sprite.h"
#include "game/world/world_queries.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/world/world_queries_api.h"

#include "game/feedback/feedback_manager.h"
#include "game/map/grid_state.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "game/map/grid_cell.h"
#include "game/map/path_data.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"
#include "game/objects/vehicle_messages.h"
#include "game/objects/vehicle_helpers.h"
#include "game/objects/object_base.h"
#include "game/objects/vehicle_update.h"
#include "game/messages/message_handler.h"
#include "game/map/path_queries.h"

#include <algorithm>
#include <cstring>
#include <intrin.h>
#include "game/ui/map_view_math.h"
#include "game/game/diagnostics.h"

// Messages, state changes and the per-frame update of vehicles, and the vehicle's own movement and animation helpers (the logic is
// vehicle_messages.h, vehicle_update.h and vehicle_helpers.h).
namespace obl::object {
using audio::CurrentLoopSounds;
using feedback::Feedback;
using game::FatalError;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::Rand;

namespace {
using map::GridCell;
using messages::Message;
using MessageHandler = messages::Handler;
using object::Vehicle;
using ui::SpriteInstance;
namespace msg = vehicle::messages;
namespace helpers = vehicle::helpers;

test::CallCounter g_receive("objects.vehicle-receive"), g_exit("objects.vehicle-exit-state"), g_enter("objects.vehicle-enter-state"),
        g_update("objects.vehicle-update");
// How often each branch ran.
test::CallCounter g_arrived("objects.vehicle-receive.moveimhere"), g_serve("objects.vehicle-receive.serve-guest"),
        g_entered("objects.vehicle-receive.guest-enter"), g_left("objects.vehicle-receive.guest-left"),
        g_animation("objects.vehicle-receive.playanimation"), g_turn("objects.vehicle-receive.stop-turn"),
        g_quality("objects.vehicle-receive.commod-quality"), g_served("objects.vehicle-exit-state.guest-served");
// Update per state, 0x41 (idle) .. 0x48 (remove), and how often a jet ski or speed boat turned its sprite to its heading.
test::CallCounter g_updated_state[] = {
        test::CallCounter("objects.vehicle-update.idle"),
        test::CallCounter("objects.vehicle-update.return"),
        test::CallCounter("objects.vehicle-update.unload"),
        test::CallCounter("objects.vehicle-update.depart"),
        test::CallCounter("objects.vehicle-update.ride"),
        test::CallCounter("objects.vehicle-update.turn"),
        test::CallCounter("objects.vehicle-update.wait"),
        test::CallCounter("objects.vehicle-update.remove"),
};
test::CallCounter g_updateHeading("objects.vehicle-update.heading");
// OnEnterState per state, 0x41 (idle) .. 0x4a.
test::CallCounter g_entered_state[] = {
        test::CallCounter("objects.vehicle-enter-state.idle"),
        test::CallCounter("objects.vehicle-enter-state.return"),
        test::CallCounter("objects.vehicle-enter-state.unload"),
        test::CallCounter("objects.vehicle-enter-state.depart"),
        test::CallCounter("objects.vehicle-enter-state.ride"),
        test::CallCounter("objects.vehicle-enter-state.turn"),
        test::CallCounter("objects.vehicle-enter-state.wait"),
        test::CallCounter("objects.vehicle-enter-state.remove"),
        test::CallCounter("objects.vehicle-enter-state.fall"),
        test::CallCounter("objects.vehicle-enter-state.remount"),
};

MessageHandler* Handler()
{
    return messages::message_facade::Current();
}
object::Base* Object(int32_t id)
{
    return object::object_manager::ObjectById(GetObjectManager(), id);
}

// The four lane changes of a vehicle leaving or returning to its stop.
enum class Lane : uint8_t { BlockDepart, UnblockDepart, BlockReturn, UnblockReturn };
void MarkLane(Lane lane, uint32_t x, uint32_t y, int32_t direction);

// ---- Env ------------------------------------------------------------------------------------------------------------------------------

// The game side of the Env of vehicle_messages.h. The vehicle's own helpers are its members.
class GameEnv {
public:
    bool BaseReceive(Vehicle& vehicle, const Message& message) { return vehicle.Mover::ReceiveMessage(const_cast<Message*>(&message)); }
    void Post(const Message& message)
    {
        Message copy = message;
        messages::message_facade::Post(Handler(), &copy);
    }
    void PostDelayed(const Message& message, float delay)
    {
        Message copy = message;
        messages::message_facade::PostDelayed(Handler(), &copy, delay);
    }
    float WorldTime() { return GetWorld()->m_gameTime; }
    void NeighbourCells(uint32_t x, uint32_t y, GridCell* out[8]) { GetGrid()->GetNeighbourCells(x, y, out); }
    void TurnToDirection(Vehicle& vehicle, int32_t direction) { vehicle.TurnToDirection(direction); }
    void MoveToNextCircuitPoint(Vehicle& vehicle) { vehicle.MoveToNextCircuitPoint(); }
    void PickRoamPoint(Vehicle& vehicle) { vehicle.PickRoamPoint(); }
    bool PickFishingPoint(Vehicle& vehicle) { return vehicle.PickFishingPoint(); }
    float PlayCatchAnimation(Vehicle& vehicle) { return vehicle.PlayCatchAnimation(); }
    float PlayBoardingAnimation(Vehicle& vehicle, float startTime, bool boarding, bool pair)
    {
        return vehicle.PlayBoardingAnimation(startTime, boarding, pair);
    }
    void TeleportGuest(int32_t guest, const float position[3])
    {
        object::Base* object = Object(guest);
        static_cast<object::Mover*>(object)->Teleport(position[0], position[1], position[2]);
    }
    void SetGuestPosition(int32_t guest, float position[3])
    {
        object::Base* object = Object(guest);
        object->SetPosition(position);
    }
    void OwnerServePosition(int32_t owner, float out[3])
    {
        building_api::GetServePosition(static_cast<const object::Building*>(Object(owner)), out);
    }
    void PlaySound(const char* name, const float position[3]) { audio::CurrentSound()->Play3DScaled(name, 1.0f, position, 1, -1); }
    void SetAnimType(void* sprite, int32_t type) { static_cast<SpriteInstance*>(sprite)->m_animType = type; }
    float StartAnim(void* sprite, const char* anim, float startTime, const float* world)
    {
        return (static_cast<SpriteInstance*>(sprite))->SetAnimByName(anim, startTime, const_cast<float*>(world));
    }

    // OnEnterState
    void PlayIdleAnimation(Vehicle& vehicle) { vehicle.PlayIdleAnimation(); }
    void PlayRideAnimation(Vehicle& vehicle, float startTime) { vehicle.PlayRideAnimation(startTime); }
    void MoveToTurnArea(Vehicle& vehicle) { vehicle.MoveToTurnArea(); }
    void TurnTowardsPoint(Vehicle& vehicle, float x, float y) { vehicle.TurnTowardsPoint(x, y); }
    void BlockDepartLane(uint32_t x, uint32_t y, int32_t direction) { MarkLane(Lane::BlockDepart, x, y, direction); }
    void UnblockDepartLane(uint32_t x, uint32_t y, int32_t direction) { MarkLane(Lane::UnblockDepart, x, y, direction); }
    void BlockReturnLane(uint32_t x, uint32_t y, int32_t direction) { MarkLane(Lane::BlockReturn, x, y, direction); }
    void UnblockReturnLane(uint32_t x, uint32_t y, int32_t direction) { MarkLane(Lane::UnblockReturn, x, y, direction); }
    uint32_t CircuitPointCount(int32_t type) { return GetWorld()->CircuitPointCount(type); }
    void* CircuitPoints(int32_t type) { return GetWorld()->CircuitPoints(type); }
    void TilePosition(const Vehicle& vehicle, uint32_t out[2])
    {
        int tile[2];
        vehicle.GetTilePosition(tile);
        out[0] = static_cast<uint32_t>(tile[0]);
        out[1] = static_cast<uint32_t>(tile[1]);
    }
    void ObjectTile(const Vehicle& vehicle, uint32_t out[2])
    {
        float position[3];
        vehicle.GetPosition(position);
        const grid_math::TileXY tile = GridInstance()->WorldPointToTile(position[0], position[1]);
        out[0] = tile.x;
        out[1] = tile.y;
    }
    void WorldPointToTile(float x, float y, uint32_t out[2])
    {
        const grid_math::TileXY tile = GridInstance()->WorldPointToTile(x, y);
        out[0] = tile.x;
        out[1] = tile.y;
    }
    void TileToPathPoint(uint32_t x, uint32_t y, float out[3])
    {
        const grid_math::PathPoint point = GetGrid()->TileToPathPoint(x, y);
        out[0] = point.x;
        out[1] = point.y;
    }
    void TileToWorldPoint(uint32_t x, uint32_t y, float out[3])
    {
        const grid_math::WorldPoint point = GetGrid()->TileToWorldPoint(x, y, 4);
        out[0] = point.x;
        out[1] = point.y;
        out[2] = point.z;
    }
    void Neighbour(uint32_t x, uint32_t y, int32_t direction, uint32_t out[2])
    {
        const grid_math::TileXY next = GetGrid()->GetNeighbour(x, y, direction);
        out[0] = next.x;
        out[1] = next.y;
    }
    GridCell* CellAt(uint32_t x, uint32_t y)
    {
        map::Grid* grid = GetGrid();
        return &grid->m_cells[grid->TileIndex(x, y)];
    }
    GridCell* WrappedCell(uint32_t x, uint32_t y)
    {
        map::Grid* grid = GetGrid();
        const auto width = static_cast<uint32_t>(grid->m_width), height = static_cast<uint32_t>(grid->m_height);
        return &grid->m_cells[(y % height) * width + x % width];
    }
    // An out-of-range direction uses the last one.
    void SetDirection(void* sprite, int32_t direction)
    {
        auto* instance = static_cast<SpriteInstance*>(sprite);
        const int32_t count = instance->m_sprite->m_anims[instance->m_animIndex]->m_directions;
        instance->m_direction = direction >= count ? count - 1 : direction;
    }
    void StartLoop(const char* name, int32_t id) { CurrentLoopSounds()->StartLoop(name, id, 1.0f, true); }
    void StopLoop(int32_t id) { CurrentLoopSounds()->StopLoop(id); }
    void SetMoving(Vehicle& vehicle, bool moving) { object::SetMovingFlag(vehicle, moving); }
    bool HasPathCursor(const Vehicle& vehicle) { return vehicle.m_pathCursor != nullptr; }
    bool PathCursorPoint(const Vehicle& vehicle, float out[3])
    {
        const auto* cursor = vehicle.m_pathCursor;
        if (!cursor) return false;
        const map::TilePoint& step = cursor->m_route->m_steps[cursor->m_currentStep];
        const uint32_t tile[2] = {step.x, step.y};
        const grid_math::WorldPoint origin = GetGrid()->TileToWorldOrigin(tile[0], tile[1]);
        const float point[3] = {origin.x, origin.y, origin.z};
        std::memcpy(out, point, sizeof(point));
        return true;
    }
    int32_t FreeRooms()
    {
        auto rooms = [](object::Base* object) {
            const auto* building = static_cast<const object::Building*>(object);
            return building->m_definition->m_GuestCapacity - building->m_NumGuests;
        };
        int32_t free = 0;
        for (object::Base* hotel : obl::object_manager::FindBuildings(*GetObjectManager(), BuildingBehavior::Hotel)) free += rooms(hotel);
        for (object::Base* block : obl::object_manager::FindBuildings(*GetObjectManager(), BuildingBehavior::AccomBlock)) {
            if (building_api::HasBuildJob(static_cast<const object::Building*>(block))) continue;
            if (building_api::HasDemolishJob(static_cast<const object::Building*>(block))) continue;
            free += rooms(block);
        }
        return free;
    }
    void NoRoomsTicker() { feedback::Feedback::OfWorld().AddKey("TICKER_MSG_NO_ROOMS", -2, 1); }
    void TurnGuestAround(int32_t guest)
    {
        auto* object = static_cast<object::Mover*>(Object(guest));
        object->m_vVelocity[0] = object->m_vVelocity[1] = 0;
        object->m_vSteerForce[0] = object->m_vSteerForce[1] = 0;
        SetDirection(object->m_sprite, (static_cast<SpriteInstance*>(object->m_sprite)->m_direction + 4) % 8);
    }
    void MarkGuestForRemoval(int32_t guest) { Object(guest)->m_waitingForDeath = 1; }
    float StartGuestAnim(int32_t guest, const char* anim, float startTime, const float* world)
    {
        return StartAnim(Object(guest)->m_sprite, anim, startTime, world);
    }
    void SetGuestAnimType(int32_t guest, int32_t type) { SetAnimType(Object(guest)->m_sprite, type); }
    int32_t Direction(void* sprite) { return static_cast<SpriteInstance*>(sprite)->m_direction; }
    int Rand() { return game::Rand(); }
    void* GuestSprite(int32_t guest) { return Object(guest)->m_sprite; }
    void* GuestAttachedSprite(int32_t guest) { return static_cast<object::Guest*>(Object(guest))->m_attachedSprite; }
    void SetGuestVisible(int32_t guest, bool visible) { Object(guest)->m_visible = visible; }
    // The first "Wharf" building, its last walk point with z = 0. False without a wharf.
    bool WharfWalkPoint(float out[3])
    {
        const auto wharves = obl::object_manager::FindBuildings(*GetObjectManager(), BuildingBehavior::ShipStop);
        if (wharves.empty()) return false;
        auto* wharf = static_cast<object::Building*>(wharves[0]);
        float point[3];
        building_api::GetWalkPoint(wharf, point, wharf->m_numWalkPoints - 1);
        out[0] = point[0];
        out[1] = point[1];
        out[2] = 0;
        return true;
    }
    bool PathIsStraight(const Vehicle& vehicle) { return path::IsCursorStraight(*vehicle.m_pathCursor); }
    void PlayTurnAnimation(Vehicle& vehicle) { vehicle.PlayTurnAnimation(); }

    // Update
    bool BaseUpdate(Vehicle& vehicle) { return vehicle.Mover::Update(); }
    void FatalError(const Vehicle& vehicle) { game::FatalError("Movement update of vehicle \"%s\" failed", vehicle.m_Name); }
    void StepTurnAnimation(Vehicle& vehicle, float now) { vehicle.UpdateTurnAnimation(now); }
    void TeleportSelf(Vehicle& vehicle, const float position[3]) { vehicle.Teleport(position[0], position[1], position[2]); }
    void MapTile(const float world[3], uint32_t out[2])
    {
        uint32_t tile[2];
        ui::MapView::WorldToTile(tile, world);
        out[0] = tile[0];
        out[1] = tile[1];
    }
    float DeltaTime() { return GetWorld()->m_gameDeltaTime; }

private:
    static map::Grid* GridInstance() { return GetGrid(); }
};

test::CallCounter g_helpers[] = {test::CallCounter("objects.vehicle-turn-to-direction"),
        test::CallCounter("objects.vehicle-turn-animation"), test::CallCounter("objects.vehicle-turn-area"),
        test::CallCounter("objects.vehicle-block-depart-lane"), test::CallCounter("objects.vehicle-unblock-depart-lane"),
        test::CallCounter("objects.vehicle-block-return-lane"), test::CallCounter("objects.vehicle-unblock-return-lane"),
        test::CallCounter("objects.vehicle-next-circuit-point"), test::CallCounter("objects.vehicle-roam-point"),
        test::CallCounter("objects.vehicle-fishing-point"), test::CallCounter("objects.vehicle-turn-towards-point"),
        test::CallCounter("objects.vehicle-idle-animation"), test::CallCounter("objects.vehicle-ride-animation"),
        test::CallCounter("objects.vehicle-catch-animation"), test::CallCounter("objects.vehicle-boarding-animation")};
void MarkLane(Lane lane, uint32_t x, uint32_t y, int32_t direction)
{
    const auto index = static_cast<size_t>(lane);
    g_helpers[3 + index].Count();
    GameEnv env;
    helpers::Lane(x, y, direction, index >= 2, index % 2 == 0, env);
}

test::CallCounter* Branch(int32_t type)
{
    switch (type) {
    case messages::MT_MOVEIMHERE: return &g_arrived;
    case messages::MT_SERVE_GUEST: return &g_serve;
    case messages::MT_GUEST_ENTER_VEHICLE: return &g_entered;
    case messages::MT_GUEST_LEFT_VEHICLE: return &g_left;
    case messages::MT_PLAYANIMATION: return &g_animation;
    case messages::MT_VEHICLE_STOP_TURN: return &g_turn;
    case messages::MT_COMMOD_QUALITY: return &g_quality;
    default: return nullptr;
    }
}

} // namespace

namespace helpers = vehicle::helpers;
namespace msg = vehicle::messages;

// The low byte of the message's address is an input of the receive logic; the world hashes depend on it.
bool Vehicle::ReceiveMessage(messages::Message* message)
{
    const auto slotByte = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(message));
    if (test::CallCounter* branch = Branch(message->type)) branch->Count();
    g_receive.Count();
    GameEnv env;
    return msg::ReceiveMessage(*this, *message, slotByte, env) != 0;
}

bool Vehicle::OnExitState()
{
    if (m_state == msg::StateUnload) g_served.Count();
    g_exit.Count();
    GameEnv env;
    return msg::OnExitState(*this, env) != 0;
}

bool Vehicle::OnEnterState(uint32_t state)
{
    if (state - msg::StateIdle < std::size(g_entered_state)) g_entered_state[state - msg::StateIdle].Count();
    g_enter.Count();
    GameEnv env;
    return msg::OnEnterState(*this, state, env) != 0;
}

bool Vehicle::Update()
{
    const int32_t state = m_state;
    if (state >= msg::StateIdle && state <= msg::StateRemove) g_updated_state[state - msg::StateIdle].Count();
    const bool heading = (m_vehicleType == vehicle::lifecycle::Jetski || m_vehicleType == vehicle::lifecycle::SpeedBoat) &&
            (state == msg::StateReturn || state == msg::StateDepart || state == msg::StateRide) && m_moving && !m_turnAnim;
    if (heading) g_updateHeading.Count();
    g_update.Count();
    GameEnv env;
    return vehicle::update::Update(*this, env);
}

void Vehicle::TurnToDirection(int32_t direction)
{
    g_helpers[0].Count();
    GameEnv env;
    helpers::TurnToDirection(*this, direction, env);
}
void Vehicle::PlayTurnAnimation()
{
    g_helpers[1].Count();
    GameEnv env;
    helpers::PlayTurnAnimation(*this, env);
}
void Vehicle::MoveToTurnArea()
{
    g_helpers[2].Count();
    GameEnv env;
    helpers::MoveToTurnArea(*this, env);
}
void Vehicle::MoveToNextCircuitPoint()
{
    g_helpers[7].Count();
    GameEnv env;
    helpers::MoveToNextCircuitPoint(*this, env);
}
void Vehicle::PickRoamPoint()
{
    g_helpers[8].Count();
    GameEnv env;
    helpers::PickRoamPoint(*this, env);
}
bool Vehicle::PickFishingPoint()
{
    g_helpers[9].Count();
    GameEnv env;
    return helpers::PickFishingPoint(*this, env);
}
void Vehicle::TurnTowardsPoint(float x, float y)
{
    g_helpers[10].Count();
    GameEnv env;
    helpers::TurnTowardsPoint(*this, x, y, env);
}
void Vehicle::PlayIdleAnimation()
{
    g_helpers[11].Count();
    GameEnv env;
    helpers::PlayIdle(*this, env);
}
void Vehicle::PlayRideAnimation(float startTime)
{
    g_helpers[12].Count();
    GameEnv env;
    helpers::PlayRide(*this, startTime, env);
}
float Vehicle::PlayCatchAnimation()
{
    g_helpers[13].Count();
    GameEnv env;
    return helpers::PlayCatch(*this, env);
}
float Vehicle::PlayBoardingAnimation(float startTime, bool boarding, bool pair)
{
    g_helpers[14].Count();
    GameEnv env;
    return helpers::PlayBoarding(*this, startTime, boarding, pair, env);
}
void Vehicle::UpdateTurnAnimation(float now)
{
    GameEnv env;
    helpers::UpdateTurnAnimation(*this, now, env);
}
} // namespace obl::object
