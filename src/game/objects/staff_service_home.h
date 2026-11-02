#pragma once

#include "game/tables/building_behavior.h"
#include "core/ascii_case.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"

#include <cstdint>
#include <cstring>

// `SetHome` of the service staff. The member is put where it works in its home
// building: a "DJ" (by its own name, any case) at the decks of a club, a "CHEF" at the grill, anyone else stays where it is
// and turns towards the building's serve position. The decks are chosen by the home's behavior (CLUB1, CLUB2), not by its name. The places
// are pixel offsets per rotation of the building, compiled into the function.
//
// Env:
//   env.Object(id)                                   the object with that id from the object manager
//   env.Behavior(home)                               the building's definition behavior, None for anything that is no building
//   env.ServePosition(home, out[3])                  the building's serve position
//   env.FaceTowards(target[3], from[3])              turning the sprite towards the target of the member's sprite
//   env.FootprintPoint(home, x, y, rotation, out[2]) the rotated footprint offset point over the building's tile and its definition's footprint
//   env.Teleport(x, y, z)                            Teleport
//   env.Send(message)                                virtual Send
namespace obl::staff::service {

struct Offset {
    float x, y;
};
// Per rotation of the building (SpriteInstance::m_direction 0..3).
inline constexpr Offset kChefOffsets[4] = {{11.0f, -152.0f}, {-12.0f, -148.0f}, {-12.0f, -126.0f}, {12.0f, -126.0f}};
inline constexpr Offset kDjClubFootprintPoint = {-206.0f, -382.0f}; // turned with the footprint by the grid
inline constexpr Offset kDjClubOffsets[4] = {{-108.0f, -424.0f}, {-44.0f, -268.0f}, {108.0f, -332.0f}, {42.0f, -486.0f}};
inline constexpr Offset kOtherDjOffsets[4] = {{64.0f, -324.0f}, {-50.0f, -338.0f}, {-66.0f, -224.0f}, {51.0f, -208.0f}};

// The sprite looks in `direction`, or in the last one its current animation has.
inline void Look(ui::SpriteInstance& sprite, int32_t direction)
{
    const int32_t directions = sprite.m_sprite->m_anims[sprite.m_animIndex]->m_directions;
    sprite.m_direction = direction < directions ? direction : directions - 1;
}

inline int32_t Rotation(const object::Base& home)
{
    return static_cast<const ui::SpriteInstance*>(home.m_sprite)->m_direction;
}

template<class Env> void SetHome(object::ServiceStaff& employee, int32_t homeId, Env& env)
{
    employee.m_HomeID = homeId;
    auto& sprite = *static_cast<ui::SpriteInstance*>(employee.m_sprite);
    if (ascii::ContainsNoCase(employee.m_Name, "DJ")) {
        const object::Base& home = *env.Object(homeId);
        const BuildingBehavior behavior = env.Behavior(home);
        const bool footprintPoint = behavior == BuildingBehavior::DjClubFootprint;
        const bool rotationOffset = behavior == BuildingBehavior::DjClubOffset;
        const int32_t rotation = Rotation(home);
        float place[2];
        if (footprintPoint) {
            env.FootprintPoint(home, kDjClubFootprintPoint.x, kDjClubFootprintPoint.y, static_cast<uint32_t>(rotation), place);
        } else {
            // A rotation beyond 3 keeps zero offsets.
            Offset offset{};
            if (static_cast<uint32_t>(rotation) <= 3) offset = (rotationOffset ? kDjClubOffsets : kOtherDjOffsets)[rotation];
            place[0] = static_cast<float>(static_cast<double>(home.m_WorldXYZ[0]) + static_cast<double>(offset.x));
            place[1] = static_cast<float>(static_cast<double>(home.m_WorldXYZ[1]) + static_cast<double>(offset.y));
        }
        env.Teleport(place[0], place[1], 0.0f);
        Look(sprite, footprintPoint ? (6 - rotation) % 4 : rotationOffset ? ((5 - rotation) * 2) % 8 : ((6 - rotation) * 2) % 8);
        messages::Message next{};
        next.sender = static_cast<int32_t>(employee.m_id);
        next.receiver = static_cast<int32_t>(employee.m_id);
        next.type = messages::MT_DO_NEXT_ACTION;
        env.Send(next);
        return;
    }
    if (ascii::ContainsNoCase(employee.m_Name, "CHEF")) {
        const object::Base& home = *env.Object(homeId);
        const int32_t rotation = Rotation(home);
        Look(sprite, (11 - rotation * 2) % 8);
        float place[3] = {home.m_WorldXYZ[0], home.m_WorldXYZ[1], home.m_WorldXYZ[2]};
        if (static_cast<uint32_t>(rotation) <= 3) {
            place[0] = static_cast<float>(static_cast<double>(place[0]) + static_cast<double>(kChefOffsets[rotation].x));
            place[1] = static_cast<float>(static_cast<double>(place[1]) + static_cast<double>(kChefOffsets[rotation].y));
        }
        env.Teleport(place[0], place[1], place[2]);
        return;
    }
    const float from[3] = {employee.m_WorldXYZ[0], employee.m_WorldXYZ[1], employee.m_WorldXYZ[2]};
    float target[3];
    env.ServePosition(*env.Object(homeId), target);
    env.FaceTowards(target, from);
}

} // namespace obl::staff::service
