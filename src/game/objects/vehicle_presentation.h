#pragma once

#include "core/text16.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_vehicle.h"
#include "game/ui/sprite.h"
#include "game/tables/world_params_tables.h"

#include <cstring>
#include <cwchar>
#include <initializer_list>

// The vehicle's Draw/GetVariableText: renderer, object queries and ANSI conversion are supplied by Env.
namespace obl::vehicle::presentation {
inline ui::SpriteInstance* Sprite(void* p)
{
    return static_cast<ui::SpriteInstance*>(p);
}
inline void Direction(void* p, int direction)
{
    auto* s = Sprite(p);
    const int count = s->m_sprite->m_anims[s->m_animIndex]->m_directions;
    s->m_direction = direction < count ? direction : count - 1;
}

template<class Env> void DrawGuests(object::Vehicle& vehicle, Env& env);

template<class Env> void Draw(object::Vehicle& vehicle, Env& env)
{
    if (vehicle.m_state == 0x47 || vehicle.m_state == 0x48) {
        const bool entry = vehicle.m_state == 0x47;
        const float* point = entry ? vehicle.m_FerryEntryPoint : vehicle.m_FerryExitPoint;
        const float* offset = entry ? vehicle.m_FerryEntryOffset : vehicle.m_FerryExitOffset;
        float world[3] = {point[0] - offset[0], point[1] - offset[1], point[2]};
        const int direction = static_cast<int32_t>(entry ? vehicle.m_ferryEntryDir : vehicle.m_ferryExitDir);
        // Modulo uses the first animation; the following clamp uses the current animation.
        Direction(vehicle.m_sprite, direction % Sprite(vehicle.m_sprite)->m_sprite->m_anims[0]->m_directions);
        env.Draw(Sprite(vehicle.m_sprite), world, env.Delta());
        if (vehicle.m_vehicleType == 5) {
            Direction(vehicle.m_doorSprite, Sprite(vehicle.m_sprite)->m_direction);
            env.Draw(Sprite(vehicle.m_doorSprite), world, env.Time());
        }
        return;
    }
    if (vehicle.m_vehicleType == 0 && (vehicle.m_state == 0x44 || vehicle.m_state == 0x42)) {
        float world[3];
        std::memcpy(world, vehicle.m_WorldXYZ, sizeof(world));
        env.Draw(Sprite(vehicle.m_vehicleSprite), world, env.Time());
    }
    if (vehicle.m_vehicleType == 6 && vehicle.m_turnAnim && vehicle.m_turnCW) {
        const int direction = Sprite(vehicle.m_sprite)->m_direction;
        const int next = (direction + 1) % 8;
        Direction(vehicle.m_sprite, next);
        Direction(vehicle.m_auxSprite1, next);
        Direction(vehicle.m_auxSprite2, next);
        Direction(vehicle.m_auxSprite3, next);
        env.BaseDraw(vehicle);
        env.Draw(Sprite(vehicle.m_auxSprite1), vehicle.m_WorldXYZ, env.Time());
        env.Draw(Sprite(vehicle.m_auxSprite2), vehicle.m_WorldXYZ, env.Time());
        env.Draw(Sprite(vehicle.m_auxSprite3), vehicle.m_WorldXYZ, env.Time());
        Direction(vehicle.m_sprite, direction);
        Direction(vehicle.m_auxSprite1, direction);
        Direction(vehicle.m_auxSprite2, direction);
        // The driver sprite deliberately keeps next; the guests return to the hull direction.
        for (uint32_t i = 0; i < static_cast<uint32_t>(vehicle.m_currCap); ++i) {
            auto* guest = env.Object(vehicle.m_Guests[i]);
            Direction(guest->m_sprite, next);
            if (!vehicle.m_waitForGuestToBoard) {
                float world[3];
                std::memcpy(world, guest->m_WorldXYZ, sizeof(world));
                const float time = env.Time();
                const float illness = env.Illness(guest);
                const float tone = static_cast<float>(env.Tone(guest));
                env.Toned(Sprite(guest->m_sprite), world, time, tone, illness);
            }
            Direction(guest->m_sprite, direction);
        }
    } else {
        env.BaseDraw(vehicle);
        for (void* part : {vehicle.m_auxSprite1, vehicle.m_auxSprite2}) {
            if (part) {
                Direction(part, Sprite(vehicle.m_sprite)->m_direction);
                env.Draw(Sprite(part), vehicle.m_WorldXYZ, env.Time());
            }
        }
        if (vehicle.m_auxSprite3) {
            const bool open = env.Open(env.Object(vehicle.m_ownerId));
            if (open || vehicle.m_state != 0x41) {
                Direction(vehicle.m_auxSprite3, Sprite(vehicle.m_sprite)->m_direction);
                env.Draw(Sprite(vehicle.m_auxSprite3), vehicle.m_WorldXYZ, env.Time());
            }
        }
        if (vehicle.m_vehicleType == 6 && vehicle.m_currCap) DrawGuests(vehicle, env);
    }
    if (vehicle.m_vehicleType == 5) {
        Direction(vehicle.m_doorSprite, Sprite(vehicle.m_sprite)->m_direction);
        env.Draw(Sprite(vehicle.m_doorSprite), vehicle.m_WorldXYZ, env.Time());
    } else if (vehicle.m_vehicleType != 2 && vehicle.m_vehicleType != 6 && vehicle.m_currCap) {
        DrawGuests(vehicle, env);
    }
}

template<class Env> void DrawGuests(object::Vehicle& vehicle, Env& env)
{
    // The first guest is drawn even while boarding; later guests are only looked up in that case.
    auto* first = env.Object(vehicle.m_Guests[0]);
    const float bias = env.Zoom() == 2 ? 2.0f : 0.0f;
    auto draw = [&](object::Base* guest) {
        Direction(guest->m_sprite, Sprite(vehicle.m_sprite)->m_direction);
        float world[3] = {vehicle.m_WorldXYZ[0], vehicle.m_WorldXYZ[1] + bias, vehicle.m_WorldXYZ[2]};
        const float time = env.Time();
        const float illness = env.Illness(guest);
        const float tone = static_cast<float>(env.Tone(guest));
        env.Toned(Sprite(guest->m_sprite), world, time, tone, illness);
    };
    draw(first);
    for (uint32_t i = 1; i < static_cast<uint32_t>(vehicle.m_currCap); ++i) {
        auto* guest = env.Object(vehicle.m_Guests[i]);
        if (!vehicle.m_waitForGuestToBoard) draw(guest);
    }
}

template<class Env> void GetVariableText(const object::Vehicle& vehicle, const char* key, Char16* out, Env& env)
{
    if (std::strcmp(key, "VEHICLE_NAME") == 0) {
        env.Wide(vehicle.m_definition->m_Name.c_str(), out);
    } else if (std::strcmp(key, "VEHICLE_STATE") == 0) {
        const Char16* text = u"Unrecognized State";
        switch (vehicle.m_state) {
        case 1: text = u"Startup"; break;
        case 0x41: text = u"Waiting Guests"; break;
        case 0x42: text = u"Heading Stop"; break;
        case 0x43: text = u"Unloading Guests"; break;
        case 0x44: text = u"Ride In Progress"; break;
        }
        text16::Copy(out, text);
    } else {
        env.Fatal("A script read an unrecognized vehicle variable");
    }
}
} // namespace obl::vehicle::presentation
