#include "game/ui/sprite_bank.h"
#include "game/objects/performer_methods.h"
#include "game/objects/object_staff.h"
#include "game/objects/object_shark.h"
#include "game/ui/furniture_runtime.h"
#include "game/map/grid_state.h"
#include "game/objects/object_base.h"
#include "game/world/world_create.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/building_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/object_base.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"
#include "core/call_counter.h"
#include "game/objects/world_create_actors.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_create_params.h"
#include "game/objects/world_create_guest.h"
#include "game/objects/world_create_building.h"
#include "game/objects/world_create_dispatch.h"
#include "game/objects/world_create_staff.h"
#include "game/objects/world_create_props.h"
#include "game/messages/message_handler.h"
#include "game/world/world_params.h"

#include <cstddef>
#include <cstdint>
#include "game/ui/map_view_math.h"
#include "game/objects/object_scenery.h"
#include "game/objects/object_static.h"
#include "game/objects/building_methods.h"
#include "game/objects/object_static.h"
#include "game/objects/object_staff.h"

namespace obl::world {
namespace {

using layout::World;
using messages::Message;
using object::StaffCreateParams;

constexpr size_t kStaffFactories = 6;

const world_create::StaffKind kStaffKinds[kStaffFactories] = {
        world_create::kServiceStaff,
        world_create::kLifeguard,
        world_create::kMechanic,
        world_create::kCleaner,
        world_create::kSecurityGuard,
        world_create::kRep,
};

// Call counters (`call-report <name>`).
test::CallCounter g_calls[kStaffFactories] = {
        test::CallCounter("world.create-service-staff"),
        test::CallCounter("world.create-lifeguard"),
        test::CallCounter("world.create-mechanic"),
        test::CallCounter("world.create-cleaner"),
        test::CallCounter("world.create-security-guard"),
        test::CallCounter("world.create-rep"),
};

// The game side of the Env contracts in src/game/objects/world_create_staff.h and world_create_actors.h.
constexpr auto kCreateSprite = [](auto* owner, auto... args) {
    return static_cast<ui::SpriteBank*>(owner)->CreateObservedInstance(args...);
};

class GameEnv {
public:
    explicit GameEnv(World& world) : m_world(world) {}

    void LookUpStaff(const char* key) { world_params::GetStaffId(game::GetWorldParams(), key); }
    // ObjectManager::CreateObject takes a key string: the record (which starts with it) or, for the performers, a literal of the game.
    object::Base* CreateObjectByKey(const char* key, object::ObjectType type)
    {
        return m_world.m_objectManager->CreateObject(key, static_cast<uint8_t>(type));
    }
    object::Base* CreateObject(const StaffCreateParams& record, object::ObjectType type) { return CreateObjectByKey(record.m_key, type); }
    void SetType(object::Base* object, object::ObjectType type) { object->SetType(static_cast<uint8_t>(type)); }
    void SetState(object::Base* object, int state) { object->SetState(static_cast<uint32_t>(state)); }
    void Teleport(object::Base* object, float x, float y, float z) { static_cast<object::Mover*>(object)->Teleport(x, y, z); }
    void SetHome(object::Base* object, int home) { static_cast<object::Staff*>(object)->SetHome(home); }
    void Post(Message& message) { messages::message_facade::Post(m_world.m_messageHandler, &message); }

    void SetHomePoint(object::Base* object, float x, float y, float z)
    {
        auto* builder = static_cast<object::Builder*>(object);
        builder->m_HomePoint[0] = x;
        builder->m_HomePoint[1] = y;
        builder->m_HomePoint[2] = z;
    }
    void SetBuilderNumber(object::Base* object, int number) { static_cast<object::Builder*>(object)->SetNumber(number); }

    void SetSprite(object::Base* object, const char* name) { object->m_sprite = kCreateSprite(ui::GetSpriteBank(), name); }
    void SetBounds(object::Base* object, const int (&words)[4])
    {
        static_cast<object::Shark*>(object)->SetBounds(words[0], words[1], words[2], words[3]);
    }
    void SetInitParam(object::Base* object, int value) { static_cast<object::Shark*>(object)->SetInitParam(value); }

    int& LitterCount() { return m_world.m_numLitter; }
    float& ScenicValue() { return m_world.m_scenicVal; }
    float GetSceneryRating(const char* key)
    {
        auto* params = game::GetWorldParams();
        const int id = world_params::GetSceneryId(params, key);
        return world_params::GetSceneryDef(params, id)->m_Rating;
    }
    void LookUpTable(const char* key)
    {
        // The result is not used: only the table id is resolved here, no definition is read.
        world_params::GetTableId(game::GetWorldParams(), key);
    }
    const char* GetVehicleSprite(const char* key)
    {
        auto* params = game::GetWorldParams();
        const int id = world_params::GetVehicleId(params, key);
        return world_params::GetVehicleDefinition(params, id)->m_Sprite.c_str();
    }
    void SetName(object::Base* object, const char* name) { static_cast<object::Scenery*>(object)->SetName(name); }
    void SetOwner(object::Base* object, int id) { static_cast<object::Scenery*>(object)->m_OwnerBuildID = id; }
    void SetTilePosition(object::Base* object, const uint32_t (&tile)[2])
    {
        float point[3] = {};
        // The wrapper ignores its object and reads the global grid, so the facade takes the same global.
        const grid_math::WorldPoint origin = game::GetGrid()->TileToWorldOrigin(tile[0], tile[1]);
        point[0] = origin.x;
        point[1] = origin.y;
        point[2] = origin.z;
        object->SetPosition(point);
    }
    void SetSpriteDirection(object::Base* object, int direction)
    {
        world_create::SetSpriteDirection(*static_cast<ui::SpriteInstance*>(object->m_sprite), direction);
    }
    void AttachTable(object::Base* object, int id) { ui::TableAttachToBuilding(*static_cast<object::Furniture*>(object), id); }
    void SetWorldXYZ(object::Base* object, float x, float y, float z) { object->SetWorldPosition(x, y, z); }
    void SetTableOrientation(object::Base* object, int orientation)
    {
        ui::TableSetOrientation(*static_cast<object::Furniture*>(object), orientation);
    }
    void SetVehicleHome(object::Base* object, int id) { static_cast<object::Vehicle*>(object)->m_ownerId = id; }
    void SetVehicleDirection(object::Base* object, int direction) { static_cast<object::Vehicle*>(object)->m_unloadDir = direction; }

protected:
    World& m_world;
};

object::Base* CreateStaff(World& world, const StaffCreateParams& record, size_t index)
{
    g_calls[index].Count();
    GameEnv env(world);
    return world_create::CreateStaff(env, record, kStaffKinds[index]);
}

// Builder and Shark: one factory each, own record.
test::CallCounter g_builderCalls("world.create-builder");
test::CallCounter g_sharkCalls("world.create-shark");

object::Base* CreateBuilder(World& world, const object::BuilderCreateParams& record)
{
    g_builderCalls.Count();
    GameEnv env(world);
    return world_create::CreateBuilder(env, record);
}

object::Base* CreateShark(World& world, const object::SharkCreateParams& record)
{
    g_sharkCalls.Count();
    GameEnv env(world);
    return world_create::CreateShark(env, record);
}

// The six performers: one template, the class decides the key literal and the two helpers.
constexpr size_t kPerformerFactories = 6;

struct PerformerFactory {
    world_create::PerformerKind kind;
    const char* key; // the script object key
    const object::PerformerMethods* methods; // <Class>_SetVariant, <Class>_AttachToBuilding
};

const PerformerFactory kPerformers[kPerformerFactories] = {
        {world_create::kWeightLifter, "WeightLifter", &object::kWeightLifterMethods},
        {world_create::kSinger, "Singer", &object::kSingerMethods},
        {world_create::kAerobic, "Aerobic", &object::kAerobicMethods},
        {world_create::kCabaret, "Cabaret", &object::kCabaretMethods},
        {world_create::kCatwalk, "Catwalk", &object::kCatwalkMethods},
        {world_create::kWetTShirt, "WetTShirt", &object::kWetTShirtMethods},
};

test::CallCounter g_performerCalls[kPerformerFactories] = {
        test::CallCounter("world.create-weight-lifter"),
        test::CallCounter("world.create-singer"),
        test::CallCounter("world.create-aerobic"),
        test::CallCounter("world.create-cabaret"),
        test::CallCounter("world.create-catwalk"),
        test::CallCounter("world.create-wet-t-shirt"),
};

class PerformerEnv : public GameEnv {
public:
    PerformerEnv(World& world, const PerformerFactory& factory) : GameEnv(world), m_factory(factory) {}

    object::Base* CreatePerformerObject() { return CreateObjectByKey(m_factory.key, m_factory.kind.type); }
    void SetVariant(object::Base* object, int variant) { m_factory.methods->setVariant(object, variant); }
    void AttachToBuilding(object::Base* object, int buildingId) { m_factory.methods->attachToBuilding(object, buildingId); }

private:
    const PerformerFactory& m_factory;
};

object::Base* CreatePerformer(World& world, const object::PerformerCreateParams& record, size_t index)
{
    g_performerCalls[index].Count();
    PerformerEnv env(world, kPerformers[index]);
    return world_create::CreatePerformer(env, record, kPerformers[index].kind);
}

test::CallCounter g_sceneryCalls("world.create-scenery");
test::CallCounter g_tableCalls("world.create-table");
test::CallCounter g_vehicleCalls("world.create-vehicle");

object::Base* CreateSceneryCounted(World& world, const object::SceneryCreateParams& record)
{
    g_sceneryCalls.Count();
    GameEnv env(world);
    return world_create::CreateScenery(env, record);
}

object::Base* CreateTableCounted(World& world, const object::TableCreateParams& record)
{
    g_tableCalls.Count();
    GameEnv env(world);
    return world_create::CreateTable(env, record);
}

object::Base* CreateVehicleCounted(World& world, const object::VehicleCreateParams& record)
{
    g_vehicleCalls.Count();
    GameEnv env(world);
    return world_create::CreateVehicle(env, record);
}

// Guest: allocation, sprite and messages stay in the game; the draw for the delayed MT_UPDATEOPINIONS is the game's CRT rand.
test::CallCounter g_guestCalls("world.create-guest");

class GuestEnv : public GameEnv {
public:
    explicit GuestEnv(World& world) : GameEnv(world) {}

    const tables::GuestDef& GetGuest(const char* key)
    {
        auto* params = game::GetWorldParams();
        const int id = world_params::GetGuestId(params, key);
        return *world_params::GetGuestDefinition(params, id);
    }
    uint8_t GlobalKeyline() { return game::GetObjectManager()->m_globalKeyline; }
    // The first message is posted through the world's handler and this one through the global (the same object in the game).
    void PostDelayed(Message& message, float delay)
    {
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &message, delay);
    }
    int Rand() { return game::Rand(); }
    int& GuestCount() { return m_world.m_numGuests; }
};

object::Base* CreateGuestCounted(World& world, const object::GuestCreateParams& record)
{
    g_guestCalls.Count();
    GuestEnv env(world);
    return world_create::CreateGuest(env, record);
}

test::CallCounter g_buildingCalls("world.create-building");

class BuildingEnv : public GameEnv {
public:
    explicit BuildingEnv(World& world) : GameEnv(world) {}

    const tables::BuildingDef& GetBuilding(const char* key)
    {
        auto* params = game::GetWorldParams();
        const int id = world_params::GetBuildingId(params, key);
        return *world_params::GetBuildingDef(params, id);
    }
    void SetBuildingPosition(object::Base* object, int x, int y)
    {
        const grid_math::WorldPoint centre = m_world.m_grid->TileToWorldPoint(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1);
        float point[3] = {centre.x, centre.y, centre.z};
        object->SetPosition(point);
    }
    void SetGridWidth(object::Base* object, uint8_t width) { static_cast<object::Prop*>(object)->SetGridWidth(width); }
    void SetGridHeight(object::Base* object, uint8_t height) { static_cast<object::Prop*>(object)->SetGridHeight(height); }
    void SetQueueDirection(object::Base* object, int rotation)
    {
        object::building_api::SetQueueDirectionFromRotation(static_cast<object::Building*>(object), rotation);
    }
    void FootprintPoint(
            float (&point)[3], const object::BuildingCreateParams& record, const tables::BuildingDef& def, const int16_t (&offset)[2])
    {
        auto* grid = game::GetGrid();
        const grid_math::PathPoint at = grid->GetFootprintOffsetPoint(static_cast<uint32_t>(record.m_tileX),
                static_cast<uint32_t>(record.m_tileY), def.m_GridWidth, def.m_GridHeight, static_cast<float>(offset[0]),
                static_cast<float>(offset[1]), static_cast<uint32_t>(record.m_orientation));
        point[0] = at.x;
        point[1] = at.y;
        point[2] = static_cast<float>((m_world.m_mapView)->GetTerrainHeightF(static_cast<int>(point[0]), static_cast<int>(point[1])));
    }
    void SetEntrancePoint(object::Base* object, float x, float y, float z)
    {
        object::building_api::SetEntrancePoint(static_cast<object::Building*>(object), x, y, z);
    }
    void SetServePoint(object::Base* object, float x, float y, float z)
    {
        object::building_api::SetServePoint(static_cast<object::Building*>(object), x, y, z);
    }
    bool BeerEnabled(int id) { return world_params::GetBeerDefinition(game::GetWorldParams(), id)->m_scriptFlag != 0; }
    void SetBeerType(object::Base* object, int id) { object::building_api::SetBeerType(static_cast<object::Building*>(object), id); }
    void SetBeerStrength(object::Base* object, float value)
    {
        object::building_api::SetBeerStrength(static_cast<object::Building*>(object), value);
    }
    void SetBroken(object::Base* object) { object::BuildingSetBroken(static_cast<object::Building*>(object)); }
    int& BuildingCount() { return m_world.m_numBuildings; }
    void InsertLastBuildingByY() { game::GetObjectManager()->InsertLastBuildingByY(); }
};

object::Base* CreateBuildingCounted(World& world, const object::BuildingCreateParams& record)
{
    g_buildingCalls.Count();
    BuildingEnv env(world);
    return world_create::CreateBuilding(env, record);
}

// The dispatcher: calls the factories above by the class number.
test::CallCounter g_dispatchCalls("world.create-object-from-message");

// Indexed by the class number (the dispatcher's request kind); class 19 has no factory (CreateSnootyStaff). The staff and
// performer indexes are the rows of kStaffKinds and kPerformers.
object::Base* CreateByType(World& world, object::ObjectType type, const void* record)
{
    const auto* staff = static_cast<const StaffCreateParams*>(record);
    const auto* performer = static_cast<const object::PerformerCreateParams*>(record);
    switch (static_cast<int>(type)) {
    case 1: return CreateSceneryCounted(world, *static_cast<const object::SceneryCreateParams*>(record));
    case 2: return CreateBuildingCounted(world, *static_cast<const object::BuildingCreateParams*>(record));
    case 3: return CreateGuestCounted(world, *static_cast<const object::GuestCreateParams*>(record));
    case 4: return CreateVehicleCounted(world, *static_cast<const object::VehicleCreateParams*>(record));
    case 5: return CreateTableCounted(world, *static_cast<const object::TableCreateParams*>(record));
    case 6: return CreateStaff(world, *staff, 0);
    case 7: return CreateStaff(world, *staff, 1);
    case 8: return CreateStaff(world, *staff, 2);
    case 9: return CreateStaff(world, *staff, 5);
    case 10: return CreateBuilder(world, *static_cast<const object::BuilderCreateParams*>(record));
    case 11: return CreateStaff(world, *staff, 3);
    case 12: return CreateStaff(world, *staff, 4);
    case 13: return CreateShark(world, *static_cast<const object::SharkCreateParams*>(record));
    case 14: return CreatePerformer(world, *performer, 3);
    case 15: return CreatePerformer(world, *performer, 1);
    case 16: return CreatePerformer(world, *performer, 0);
    case 17: return CreatePerformer(world, *performer, 4);
    case 18: return CreatePerformer(world, *performer, 5);
    case 20: return CreatePerformer(world, *performer, 2);
    default: return nullptr;
    }
}

class DispatchEnv : public GameEnv {
public:
    explicit DispatchEnv(World& world) : GameEnv(world) {}

    object::Base* CreateObject(object::ObjectType type, const void* record) { return CreateByType(m_world, type, record); }
    uint8_t& Paused() { return m_world.m_paused; }
    void PostGlobal(Message& message) { messages::message_facade::Post(messages::message_facade::Current(), &message); }
};

} // namespace

bool CreateObjectFromMessage(World& world, const Message& request, const void* record)
{
    g_dispatchCalls.Count();
    DispatchEnv env(world);
    return world_create::CreateObjectFromMessage(env, request, record);
}

object::Base* CreateObject(World& world, object::ObjectType type, const void* record)
{
    return CreateByType(world, type, record);
}

} // namespace obl::world
