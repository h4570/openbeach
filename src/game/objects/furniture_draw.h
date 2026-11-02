#pragma once

#include "game/objects/object_base.h"
#include "core/ascii_case.h"
#include "game/ui/sprite.h"
#include "game/objects/object_table.h"

#include <cstdint>
#include <cstring>

// The table's Draw (docs/architecture/objects.md). Pure logic: what it draws goes through an `Env`
// that the game side maps onto the sprite functions of the renderer (src/game/ui/furniture_runtime.cpp) and the unit test onto a recorder.
//
// Env (positions are `float[3]` copies the callee may keep or change):
//   float Time                                                       world game time
//   object::Base* Object(int32_t id)                             the object manager's lookup
//   void WorldDrawPos(SpriteInstance*, float* world, float time, int& x, int& y, int16_t& depth)   the animated sprite drawing position (advances the animation)
//   void DrawInWorld(SpriteInstance*, float* world, float time)        drawing the sprite in world space
//   void DrawClipped(SpriteInstance*, int x, int y, uint32_t depth, float time)                   clipped sprite drawing
//   float GuestIllness(Entity&), float GuestTone(Entity&)      the guest's GetFoodIllness / GetPaletteTone (called in this order)
//   void DrawInWorldToned(SpriteInstance*, float* world, float time, float tone, float illness)   drawing the sprite with lighting and illness tones
//   void DrawClippedToned(SpriteInstance*, int x, int y, uint32_t depth, float time, float tone, float illness)   clipped sprite drawing with lighting and illness tones
namespace obl::table {

inline constexpr char kNoSprite[] = "NONE"; // k_GuiTableButtonNoAction
inline constexpr char kJacuzziBubble[] = "Jacuzzi_bubble"; // this table sprite follows the owning building, and bubbles with it
inline constexpr char kShowerWater[] = "Beach_shower_water"; // the same for a chair sprite
inline constexpr uint32_t kWaiterDepthBias = 0x80; // added to the table's draw depth for the waiter drawn in front of it

namespace detail {
inline ui::SpriteInstance* SpriteOf(const object::Base& object)
{
    return static_cast<ui::SpriteInstance*>(object.m_sprite);
}
inline void Copy3(float out[3], const float in[3])
{
    std::memcpy(out, in, 3 * sizeof(float));
}
} // namespace detail

// The table sprite's draw depth is a 32-bit value of which GetWorldDrawPos stores the low word only; the upper word stays zero (and all of it when
// the table sprite is "NONE").
template<class Env> void Draw(object::Furniture& table, Env& env)
{
    using detail::Copy3;
    const tables::TableDef& definition = *table.m_definition;
    const float time = env.Time();
    const bool tableSprite = !ascii::EqualsNoCase(definition.m_TableSprite, kNoSprite);
    auto* sprite = detail::SpriteOf(table);

    uint32_t depth = 0;
    int x = 0, y = 0;
    if (tableSprite) {
        float world[3];
        Copy3(world, table.m_WorldXYZ);
        int16_t word = 0;
        env.WorldDrawPos(sprite, world, time, x, y, word);
        depth = static_cast<uint16_t>(word);
        // The sprite (name and offsets of the definition): the jacuzzi bubbles at the building's position, all others at the table's.
        const object::Base* base = definition.m_TableSprite == kJacuzziBubble ? env.Object(table.m_ownerId) : &table;
        float point[3] = {static_cast<float>(definition.m_XOffsetTable) + base->m_WorldXYZ[0],
                static_cast<float>(definition.m_YOffsetTable) + base->m_WorldXYZ[1], base->m_WorldXYZ[2]};
        env.DrawInWorld(sprite, point, time);
    }
    if (table.m_serveTable) return;

    if (definition.m_hasPlates && table.m_drawWaiter) {
        object::Base* waiter = env.Object(table.m_WaiterID);
        float world[3];
        Copy3(world, waiter->m_WorldXYZ);
        int16_t unused = 0;
        env.WorldDrawPos(detail::SpriteOf(*waiter), world, time, x, y, unused);
        env.DrawClipped(detail::SpriteOf(*waiter), x, y, depth + kWaiterDepthBias, time);
    }
    for (uint32_t i = 0; i < definition.m_NumChairs; ++i) {
        if (table.m_chairSprites) {
            ui::SpriteInstance* chair = table.m_chairSprites[i];
            float point[3];
            if (ascii::EqualsNoCase(chair->m_name, kShowerWater))
                Copy3(point, env.Object(table.m_ownerId)->m_WorldXYZ);
            else
                Copy3(point, table.m_WorldXYZ);
            env.DrawInWorld(chair, point, time);
        }
        if (definition.m_hasPlates && table.m_pbDrawPlate[i]) {
            float point[3];
            Copy3(point, table.m_WorldXYZ);
            env.DrawInWorld(table.m_plateSprites[i], point, time);
        }
        const object::TableChair& seat = table.m_chairs[i];
        if (seat.m_ChairOccupier == -1 || !seat.m_ChairSeated) continue;
        object::Base* guest = env.Object(seat.m_ChairOccupier);
        float point[3];
        Copy3(point, guest->m_WorldXYZ);
        if (!tableSprite) {
            const float illness = env.GuestIllness(*guest);
            const float tone = env.GuestTone(*guest);
            env.DrawInWorldToned(detail::SpriteOf(*guest), point, time, tone, illness);
        } else {
            int16_t unused = 0;
            env.WorldDrawPos(detail::SpriteOf(*guest), point, time, x, y, unused);
            const float illness = env.GuestIllness(*guest);
            const float tone = env.GuestTone(*guest);
            env.DrawClippedToned(detail::SpriteOf(*guest), x, y, depth, time, tone, illness);
        }
    }
}

} // namespace obl::table
