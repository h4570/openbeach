#pragma once

#include <cstdint>

// Scenery state entry, updates and message handling use pure decisions (docs/architecture/objects.md):
// src/game/objects/scenery_state.cpp and scenery_message.cpp read the fields, call these and keep every game call.
namespace obl::scenery {

// --- scenery message handling -------------------------------------------------------------------------------------------------------

// MT_STATECHANGE only seats a guest when the sender is somebody else: not the bench itself and not the two system senders 0xfffffffe
// and 0xfffffffd (the guest-class check stays in the glue, it needs the object manager).
inline bool IsSeatSender(uint32_t sender, uint32_t sceneryId)
{
    return sender != sceneryId && sender != 0xfffffffeu && sender != 0xfffffffdu;
}

// The first seat equal to `id` (-1 = free), or `maxSeats` when there is none; callers write to that index anyway.
inline uint32_t FindSeat(const int32_t* seats, int32_t maxSeats, int32_t id)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(maxSeats); ++i)
        if (seats[i] == id) return i;
    return static_cast<uint32_t>(maxSeats);
}

// MT_REPAIR steps the repair state down by one (1 -> 0, 2 -> 1, anything above -> 2); 0 stays 0.
inline uint32_t NextRepairState(uint32_t state)
{
    return state == 0 ? 0 : state == 1 ? 0 : state == 2 ? 1 : 2;
}

// The reply to the mechanic: 91 = more work left, 92 = repaired.
inline uint32_t RepairReplyType(uint32_t state)
{
    return state ? 91 : 92;
}

// Message 84 (litter nuisance): guests near vandalised scenery get data 2, near vomit 1.
inline uint32_t NuisanceLevel(bool vandalised)
{
    return vandalised ? 2 : 1;
}

// --- entering a scenery state ---------------------------------------------------------------------------------------------------------

// Only state 1 (placed) and 3 (sold) do anything; every state answers 1.
inline bool EnterStateActs(uint32_t state)
{
    return state == 1 || state == 3;
}

// A lounger faces one step clockwise from the first ray that hit a water cell, limited to the directions its animation has.
inline int LoungerDirection(int ray, int directions)
{
    const int direction = (ray + 1) % 8;
    return direction >= directions ? directions - 1 : direction;
}

// Litter lands up to 30 px off the tile centre in x and 14 px in y; `r` is one `rand()` each (x first).
inline float LitterJitterX(int r)
{
    return static_cast<float>(r % 60 - 30);
}
inline float LitterJitterY(int r)
{
    return static_cast<float>(r % 28 - 14);
}

// Other scenery snaps to point 1 of its tile for hoardings (name contains "HOARDING_") and point 4 (centre) otherwise.
inline int SnapPoint(bool hoarding)
{
    return hoarding ? 1 : 4;
}

// Selling refunds 20% of the build cost: unsigned cost times the float 0.2 widened to double, plus the cash, stored as float.
inline float SellRefund(float cash, uint32_t cost)
{
    return static_cast<float>(static_cast<double>(cost) * static_cast<double>(0.2f) + cash);
}

// A cell flag (0x8000 collide, 0x40 vegetation) set on placement clears the dirty bit 31; removal clears the flag and sets bit 31.
inline uint32_t ApplyCellFlag(uint32_t cellFlags, uint32_t flag, bool placing)
{
    return placing ? (cellFlags | flag) & 0x7fffffffu : (cellFlags & ~flag) | 0x80000000u;
}

// --- updating scenery ---------------------------------------------------------------------------------------------------------------

// Idle vandalised scenery that can be repaired asks for a mechanic once (neither "called" flag set yet); while one is on the way a
// light, if any, flickers. Only Flicker consumes a rand() roll (FlashLight), so the glue must not roll in the other cases.
enum class RepairAction { None, CallMechanic, Flicker };

inline RepairAction RepairUpdate(bool canRepair, bool vandalised, bool called, bool called5f, bool hasLight)
{
    if (!canRepair || !vandalised) return RepairAction::None;
    if (!called && !called5f) return RepairAction::CallMechanic;
    return hasLight ? RepairAction::Flicker : RepairAction::None;
}

// The flicker is a 1-in-20 roll.
inline bool FlashLight(int r)
{
    return r % 20 == 0;
}

// The guest states that may hold a seat; any other one is the "Guest thinks they've no Longer got" fatal error.
inline bool IsSeatedGuestState(int state)
{
    return state == 53 || state == 24 || state == 29 || state == 51;
}

} // namespace obl::scenery
