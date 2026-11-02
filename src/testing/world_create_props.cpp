#include "game/ui/sprite.h"
#include "game/world/world_create.h"
#include "game/objects/object_base.h"
#include "world_create_props.h"

#include "game/objects/object_scenery.h"
#include "game/objects/object_table.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_vehicle.h"
#include "game/objects/object_create_params.h"
#include "core/log.h"
#include "test_util.h"
#include "world_state.h"

#include <cstring>

namespace obl::test {
namespace {
void Check(bool condition, const char* detail)
{
    OBL_LOG("autotest: %s world-create-props %s", condition ? "PASS" : "FAILED", detail);
}
} // namespace

bool ExecuteWorldCreatePropsCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "world-create-props") return false;
    auto* world = World();
    if (!args.empty() || !world || GameStateIndex() != 0 || Game()->m_guiReloadPending) {
        Check(false, "requires settled gameplay and no arguments");
        return true;
    }
    // Exercise both the building-owned path (tables, waiter creation and hire vehicles)
    // and direct factory records whose optional fields scenario startup seldom supplies.
    auto* cafe = SpawnBuilding("CAFE1", 80, 45);
    auto* hire = SpawnBuilding("NJJETSKIHIRE", 80, 65);
    if (!cafe || !hire) return true;

    object::SceneryCreateParams scenery{};
    std::strcpy(scenery.m_key, "BENCH");
    std::strcpy(scenery.m_name, "FactoryBench");
    scenery.m_tile[0] = 75;
    scenery.m_tile[1] = 45;
    scenery.m_ownerBuildId = -2;
    const auto createScenery = [](world_data::World* w, const object::SceneryCreateParams* r) {
        return static_cast<object::Scenery*>(world::CreateObject(*w, object::ObjectType::Scenery, r));
    };
    const float ratingBefore = world->m_scenicVal;
    auto* a = createScenery(world, &scenery);
    if (!a) {
        Check(false, "bench creation");
        return true;
    }
    Check(a->m_type == object::ObjectType::Scenery && std::strcmp(a->m_Name, scenery.m_name) == 0, "scenery class/name");
    Check(world->m_scenicVal == a->m_definition->m_Rating + ratingBefore, "scenery rating accumulated");
    uint32_t tile[2] = {};
    a->GetTilePosition(reinterpret_cast<int32_t*>(tile));
    Check(tile[0] == scenery.m_tile[0] && tile[1] == scenery.m_tile[1], "scenery tile after startup placement");
    scenery.m_ownerBuildId = cafe->m_id;
    scenery.m_tile[0] = 76;
    scenery.m_setDirection = 1;
    scenery.m_direction = 255;
    a = createScenery(world, &scenery);
    if (!a) {
        Check(false, "directed bench creation");
        return true;
    }
    auto* sprite = static_cast<ui::SpriteInstance*>(a->m_sprite);
    Check(a->m_OwnerBuildID == static_cast<int>(cafe->m_id), "scenery owner override");
    Check(sprite->m_direction == sprite->m_sprite->m_anims[sprite->m_animIndex]->m_directions - 1, "scenery direction upper clamp");

    const int litterBefore = world->m_numLitter;
    const float scenicBefore = world->m_scenicVal;
    std::strcpy(scenery.m_key, "Littercancrush");
    std::strcpy(scenery.m_name, "SomeLitterHere");
    scenery.m_ownerBuildId = -2;
    scenery.m_setDirection = 0;
    scenery.m_tile[0] = 77;
    world->m_numLitter = 499;
    a = createScenery(world, &scenery);
    Check(a && world->m_numLitter == 500 && world->m_scenicVal == scenicBefore, "litter 499 accepted without rating");
    const auto nextId = world->m_objectManager->m_nextFreeObjectId;
    a = createScenery(world, &scenery);
    Check(!a && world->m_numLitter == 500 && world->m_objectManager->m_nextFreeObjectId == nextId, "litter 500 rejected before allocation");
    world->m_numLitter = litterBefore + 1;

    object::TableCreateParams table{};
    std::strcpy(table.m_key, "CAFETABLE1");
    table.m_pos[0] = cafe->m_WorldXYZ[0];
    table.m_pos[1] = cafe->m_WorldXYZ[1];
    table.m_pos[2] = 0.25f;
    table.m_buildingId = cafe->m_id;
    table.m_orientation = 3;
    auto* t = static_cast<object::Furniture*>(world::CreateObject(*world, object::ObjectType::Table, &table));
    Check(t && t->m_type == object::ObjectType::Table && t->m_ownerId == table.m_buildingId && t->m_rotation == 3 &&
                    std::memcmp(t->m_WorldXYZ, table.m_pos, sizeof(table.m_pos)) == 0,
            "table owner/rotation/XYZ");

    object::VehicleCreateParams vehicle{};
    std::strcpy(vehicle.m_key, "JETSKIVEHICLE");
    vehicle.m_pos[0] = hire->m_WorldXYZ[0];
    vehicle.m_pos[1] = hire->m_WorldXYZ[1];
    vehicle.m_pos[2] = 0.0f;
    vehicle.m_homeId = hire->m_id;
    vehicle.m_direction = 255;
    auto* v = static_cast<object::Vehicle*>(world::CreateObject(*world, object::ObjectType::Vehicle, &vehicle));
    if (!v) {
        Check(false, "vehicle creation");
        return true;
    }
    sprite = static_cast<ui::SpriteInstance*>(v->m_sprite);
    Check(v->m_type == object::ObjectType::Vehicle && v->m_ownerId == vehicle.m_homeId && v->m_unloadDir == 255,
            "vehicle owner and unclamped unload direction");
    Check(sprite->m_direction == sprite->m_sprite->m_anims[sprite->m_animIndex]->m_directions - 1, "vehicle sprite direction upper clamp");
    return true;
}
} // namespace obl::test
