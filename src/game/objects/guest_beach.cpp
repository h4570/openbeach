#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/objects/guest_beach_points.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "core/call_counter.h"

#include <cstdint>

namespace obl::object {
using game::GetGrid;
using game::Rand;

namespace {

using guest_beach::BeachPoints;
using object::Guest;

// Call counters (`call-report <name>`).
test::CallCounter g_repeat("objects.guest-set-repeat-action");
test::CallCounter g_sunbathe("objects.guest-find-sunbathe-point");
test::CallCounter g_swim("objects.guest-find-swim-point");
test::CallCounter g_nearestSea("objects.guest-find-nearest-sea-point");

// The result of each search, handed back to the script as a pointer, so each lives in a static scratch buffer.
BeachPoints g_sunbatheResult{};
BeachPoints g_swimResult{};
BeachPoints g_nearestSeaResult{};
BeachPoints* SunbatheBuffer()
{
    return &g_sunbatheResult;
}
BeachPoints* SwimBuffer()
{
    return &g_swimResult;
}
BeachPoints* NearestSeaBuffer()
{
    return &g_nearestSeaResult;
}

// ---- SetRepeatAction -------------------------------------------------------------------------------------------------

void SetRepeat(Guest& guest, int desire, float value)
{
    guest.m_RepeatAction = guest_beach::RepeatAction(desire, guest.m_definition->m_Max, value, guest.m_RepeatAction);
}

// ---- FindSunbathePoint -----------------------------------------------------------------------------------------------

float* Sunbathe(Guest* guest)
{
    const float position[3] = {guest->m_WorldXYZ[0], guest->m_WorldXYZ[1], guest->m_WorldXYZ[2]};
    BeachPoints points;
    switch (guest_beach::FindSunbathePoint(*GetGrid(), position, [] { return Rand(); }, points)) {
    case guest_beach::Found::Unhandled: fatal::UnhandledInput("objects.guest-find-sunbathe-point", "the beach redraw loop ran out");
    case guest_beach::Found::Done: *SunbatheBuffer() = points; break;
    case guest_beach::Found::Nothing: break;
    }
    return reinterpret_cast<float*>(SunbatheBuffer());
}

// ---- FindSwimPoint ---------------------------------------------------------------------------------------------------

float* Swim(float x, float y)
{
    BeachPoints points;
    switch (guest_beach::FindSwimPoint(*GetGrid(), x, y, [] { return Rand(); }, points)) {
    case guest_beach::Found::Unhandled: fatal::UnhandledInput("objects.guest-find-swim-point", "the sea search ran out");
    case guest_beach::Found::Nothing: return nullptr;
    case guest_beach::Found::Done: *SwimBuffer() = points; break;
    }
    return reinterpret_cast<float*>(SwimBuffer());
}

// ---- FindNearestSeaPoint ---------------------------------------------------------------------------------------

// `this` is not read; z is not either (the vertices have z = 0 and the distance does not subtract it). The result buffer is written only when the
// search finds a point, and the pointer to it is always returned.
float* NearestSea(float x, float y)
{
    BeachPoints points;
    switch (guest_beach::FindNearestSeaPoint(*GetGrid(), x, y, [] { return Rand(); }, points)) {
    case guest_beach::Found::Unhandled: fatal::UnhandledInput("objects.guest-find-nearest-sea-point", "the sea search ran out");
    case guest_beach::Found::Nothing: break;
    case guest_beach::Found::Done: *NearestSeaBuffer() = points; break;
    }
    return reinterpret_cast<float*>(NearestSeaBuffer());
}

} // namespace

void Guest::SetRepeatAction(int desire, float value)
{
    g_repeat.Count();
    SetRepeat(*this, desire, value);
}

float* Guest::FindSunbathePoint()
{
    g_sunbathe.Count();
    return Sunbathe(this);
}

float* GuestFindSwimPoint(float x, float y)
{
    g_swim.Count();
    return Swim(x, y);
}

float* GuestFindNearestSeaPoint(float x, float y)
{
    g_nearestSea.Count();
    return NearestSea(x, y);
}

} // namespace obl::object
