#pragma once

#include "game/objects/scenery_names.h"
#include "game/ui/sprite.h"
#include "core/ascii_case.h"
#include "game/objects/object_create_params.h"
#include "game/objects/world_create_staff.h"

#include <cstring>

// Env extends the existing factory contract with LitterCount()/ScenicValue() references,
// GetSceneryRating(key), LookUpTable(key), GetVehicleSprite(key), SetName, SetOwner,
// SetTilePosition, SetSpriteDirection, AttachTable, SetWorldXYZ, SetTableOrientation,
// SetVehicleHome and SetVehicleDirection. Constructors and object methods stay in the game.
namespace obl::world_create {

// Clamps at the top only: negative requests stay negative, and an
// animation with zero directions produces -1.
inline void SetSpriteDirection(ui::SpriteInstance& sprite, int direction)
{
    const int count = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
    sprite.m_direction = direction < count ? direction : count - 1;
}

template<class Env> object::Base* CreateScenery(Env& env, const object::SceneryCreateParams& record)
{
    if (scenery_names::HasLitterInName(record.m_name)) {
        if (static_cast<uint32_t>(env.LitterCount()) >= 500) return nullptr;
        ++env.LitterCount();
    } else {
        const float rating = env.GetSceneryRating(record.m_key);
        env.ScenicValue() = rating + env.ScenicValue();
    }
    auto* object = env.CreateObjectByKey(record.m_key, object::ObjectType::Scenery);
    env.SetName(object, record.m_name);
    env.SetType(object, object::ObjectType::Scenery);
    env.SetState(object, 0);
    if (record.m_ownerBuildId != -2) env.SetOwner(object, record.m_ownerBuildId);
    env.SetTilePosition(object, record.m_tile);
    if (record.m_setDirection) env.SetSpriteDirection(object, record.m_direction);
    messages::Message message = StartMessage(object->m_id);
    env.Post(message);
    return object;
}

template<class Env> object::Base* CreateTable(Env& env, const object::TableCreateParams& record)
{
    env.LookUpTable(record.m_key);
    auto* object = env.CreateObjectByKey(record.m_key, object::ObjectType::Table);
    env.AttachTable(object, record.m_buildingId);
    env.SetType(object, object::ObjectType::Table);
    env.SetWorldXYZ(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetTableOrientation(object, record.m_orientation);
    messages::Message message = StartMessage(object->m_id);
    env.Post(message);
    return object;
}

template<class Env> object::Base* CreateVehicle(Env& env, const object::VehicleCreateParams& record)
{
    const char* sprite = env.GetVehicleSprite(record.m_key);
    auto* object = env.CreateObjectByKey(record.m_key, object::ObjectType::Vehicle);
    env.SetVehicleHome(object, record.m_homeId);
    env.SetType(object, object::ObjectType::Vehicle);
    env.SetState(object, 1);
    env.Teleport(object, record.m_pos[0], record.m_pos[1], record.m_pos[2]);
    env.SetSprite(object, sprite);
    env.SetSpriteDirection(object, record.m_direction);
    env.SetVehicleDirection(object, record.m_direction);
    messages::Message message = StartMessage(object->m_id);
    env.Post(message);
    return object;
}

} // namespace obl::world_create
