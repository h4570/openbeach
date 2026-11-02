#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "script_types.h"

#include <cstdint>

// The four methods of Person (the class between Mover and the guest / staff classes, docs/architecture/objects.md) that only pass on to
// Mover, except one script number:
//   GetValue                (key, data): key 0x110 ("no value") answers {0x110, data} itself
//   SetValue                (key, data, type, bits)
//   ReceiveMovementMessage  (Message*), returns the base class's bool
// The class adds no fields to what it reads or writes (its three words after Mover are saved by the guest and staff classes). The calls
// into the base go through an `Env` so that the logic is unit-tested on its own:
//   ScriptValue BaseGet(Person&, key, data)                 the Mover GetValue (the out value comes back by value)
//   void BaseSet(Person&, key, data, type, bits)            the Mover SetValue
//   bool BaseReceive(Person&, Message*)                     the Mover ReceiveMessage
namespace obl::human {

using object::Person;
using script::ScriptValue;

inline constexpr int32_t kKeyNone = 0x110; // script_program::exec::kKeyNone, the value kind of "no value"

template<class Env> ScriptValue GetValue(Person& human, int32_t key, intptr_t data, Env& env)
{
    if (key == kKeyNone) return {kKeyNone, static_cast<int32_t>(data)};
    return env.BaseGet(human, key, data);
}

template<class Env> void SetValue(Person& human, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    env.BaseSet(human, key, data, type, bits);
}

template<class Env, class Message> bool ReceiveMovementMessage(Person& human, Message* message, Env& env)
{
    return env.BaseReceive(human, message);
}

// ---- step 2: life cycle, Update, selection ---------------------------------------------------------------------------------------------------
// Update (char) and TrySelectAtScreenPoint (bool); Person adds nothing to Mover's construction and teardown. More of the Env:
//   bool BaseUpdate(Person&)                                the Mover Update
//   void Fatal(const char* name)                                 FatalError("Movement update of human \"%s\" failed", name)
//   TrySelect: WorldToScreen(world, out) / DrawOffsetX / HotspotY / FirstOpaquePixel(x, y) / HitTest(point, screenPoint) / Select
//              as in guest_selection.h; Select is the object's own OnSelect (OnSelect(1, 0)).

// A failing base update ends the game with a FatalError naming the object; the answer is 1 whenever the call returns (a test Env may return).
template<class Env> bool Update(Person& human, Env& env)
{
    if (!env.BaseUpdate(human)) env.Fatal(human.m_Name);
    return true;
}

// Only a visible object can be picked: the selection region hangs off the first opaque pixel of its sprite as drawn.
template<class Env> bool TrySelectAtScreenPoint(Person& human, int32_t* screenPoint, Env& env)
{
    if (!human.m_visible) return false;
    const float world[3] = {human.m_WorldXYZ[0], human.m_WorldXYZ[1], human.m_WorldXYZ[2]};
    int32_t point[2] = {};
    env.WorldToScreen(world, point);
    point[0] += env.DrawOffsetX();
    point[1] += env.HotspotY();
    int32_t pixelX = 0, pixelY = 0;
    env.FirstOpaquePixel(pixelX, pixelY);
    point[0] += pixelX;
    point[1] += pixelY;
    if (!env.HitTest(point, screenPoint)) return false;
    env.Select();
    return true;
}

} // namespace obl::human
