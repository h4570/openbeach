#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_performers.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"

#include <cstdint>

// AttachToBuilding of the six performers (docs/architecture/objects.md, "AttachToBuilding"): remember the show building, join its event actors and, except
// for the cabaret, take the point on the stage the performer works at.
//   Env::Building(int id)                                    the object with that id, a Building
//   Env::AddActor(Building&, int objectId)             adds the actor to building.m_events
//   Env::FootprintPoint(Building&, float offsetX, float offsetY, float (&out)[2])
//        a footprint offset rotated around the building's tile according to its definition size and orientation
// The stage offsets are fixed pairs: (6, -286) for the stage acts, (13, -274) for the wet T-shirt contest.
namespace obl::performer {

namespace detail {
constexpr float kStageOffsetX = 6.0f, kStageOffsetY = -286.0f;
constexpr float kContestOffsetX = 13.0f, kContestOffsetY = -274.0f;

inline int32_t Rotation(const object::Building& building)
{
    return static_cast<const ui::SpriteInstance*>(building.m_sprite)->m_direction;
}

template<class Env> object::Building& Join(int32_t& owner, int buildingId, int32_t actorId, Env& env)
{
    owner = buildingId;
    object::Building& building = env.Building(buildingId);
    env.AddActor(building, actorId);
    return building;
}
template<class Env> void Point(object::Building& building, float (&point)[3], float offsetX, float offsetY, Env& env)
{
    float out[2];
    env.FootprintPoint(building, offsetX, offsetY, out);
    point[0] = out[0];
    point[1] = out[1];
    point[2] = 0.0f;
}
} // namespace detail

template<class Env> void AttachToBuilding(object::Aerobic& performer, int buildingId, Env& env)
{
    auto& building = detail::Join(performer.m_OwnerID, buildingId, static_cast<int32_t>(performer.m_id), env);
    detail::Point(building, performer.m_ActionPoint, detail::kStageOffsetX, detail::kStageOffsetY, env);
}

template<class Env> void AttachToBuilding(object::Cabaret& performer, int buildingId, Env& env)
{
    detail::Join(performer.m_OwnerID, buildingId, static_cast<int32_t>(performer.m_id), env);
}

// A catwalk model is hidden for the two rotations that face away from the audience.
template<class Env> void AttachToBuilding(object::Catwalk& performer, int buildingId, Env& env)
{
    auto& building = detail::Join(performer.m_OwnerID, buildingId, static_cast<int32_t>(performer.m_id), env);
    const int32_t rotation = detail::Rotation(env.Building(performer.m_OwnerID));
    if (rotation == 2 || rotation == 1) performer.m_visible = 0;
    detail::Point(building, performer.m_ActionPoint, detail::kStageOffsetX, detail::kStageOffsetY, env);
}

// The microphone faces the audience: direction (11 - 2 * rotation) modulo 8, clamped to the last direction of the animation it plays.
template<class Env> void AttachToBuilding(object::Singer& performer, int buildingId, Env& env)
{
    auto& building = detail::Join(performer.m_OwnerID, buildingId, static_cast<int32_t>(performer.m_id), env);
    detail::Point(building, performer.m_ActionPoint, detail::kStageOffsetX, detail::kStageOffsetY, env);
    const int32_t direction = (11 - 2 * detail::Rotation(env.Building(performer.m_OwnerID))) % 8;
    auto* mic = static_cast<ui::SpriteInstance*>(performer.m_attachedSprite);
    const int directions = mic->m_sprite->m_anims[mic->m_animIndex]->m_directions & 0xff;
    mic->m_direction = direction < directions ? direction : directions - 1;
}

template<class Env> void AttachToBuilding(object::WeightLifter& performer, int buildingId, Env& env)
{
    auto& building = detail::Join(performer.m_OwnerID, buildingId, static_cast<int32_t>(performer.m_id), env);
    detail::Point(building, performer.m_ActionPoint, detail::kStageOffsetX, detail::kStageOffsetY, env);
}

template<class Env> void AttachToBuilding(object::WetTShirt& performer, int buildingId, Env& env)
{
    auto& building = detail::Join(performer.m_OwnerID, buildingId, static_cast<int32_t>(performer.m_id), env);
    detail::Point(building, performer.m_ActionPoint, detail::kContestOffsetX, detail::kContestOffsetY, env);
}

} // namespace obl::performer
