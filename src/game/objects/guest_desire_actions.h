#pragma once

#include "core/ascii_case.h"
#include "game/objects/guest_update_opinions.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// MissedDesire, AlterDesires, SetupStartValues (guest desires: satisfaction and misses, docs/architecture/objects.md).
// The first two find the desire the guest's current building served: the family key of the commodity the guest chose (m_DestObjectID's building,
// m_desired_commod) is compared with the 26 desire names (guest_search::kDesireNames, same order as m_desires) without regard to case. They run from the
// script (guest CallFunction ids 5 and 0x1f) and from the visit helpers; none of the three draws or sends anything.
namespace obl::guest_desire_actions {

using object::Guest;
using tables::GuestDef;

constexpr int kDesireCount = 26;
// Indices into Guest::m_desires that the code names (docs/architecture/objects.md).
constexpr int kAlcohol = 1, kMeals = 5, kSnacks = 8, kSoftDrinks = 9, kSunbathing = 12, kSwimming = 13, kToilet = 14, kStroll = 16;
constexpr int kVandalise = 18, kFight = 19, kFlirt = 20;

constexpr float kToiletRelief = 0.1f; // added to the bladder desire after a drink or a meal
constexpr int32_t kStateWc = 0xe; // the toilet state: the desire is not raised while the guest sits there
constexpr const char* kWalkNormal = "D_WALK_NORMAL";
constexpr const char* kStandNormal = "D_STAND_NORMAL";

// The first desire at or after `from` whose name is `family` (the game compares the bytes up to the terminating zero), kDesireCount when none.
inline int FindDesire(const char* family, const char* const* names, int from = 0)
{
    for (int i = from; i < kDesireCount; ++i)
        if (ascii::EqualsNoCase(family, names[i])) return i;
    return kDesireCount;
}

// The three outdoor desires also reset the activity byte.
inline bool IsOutdoor(int desire)
{
    return desire == kSunbathing || desire == kSwimming || desire == kStroll;
}

// MissedDesire after the building and its commodity were resolved: only the first desire of that name counts (the loop ends there). The first miss
// of a search round just sets the flag (m_missedDesireLast is the previous round's flag); a repeated miss clears the desire, the activity of an outdoor
// one, and costs the guest the variety opinion unless the desire is outdoor or Toilet (held at zero by the unrounded value).
inline void MissedDesire(Guest& g, const GuestDef& def, const char* family, const char* const* names)
{
    const int desire = FindDesire(family, names);
    if (desire == kDesireCount) return;
    if (!g.m_missedDesireLast) {
        g.m_missedDesire = 1;
        return;
    }
    g.m_desires[desire] = 0.0f;
    if (IsOutdoor(desire)) g.m_activity = 0;
    if (IsOutdoor(desire) || desire == kToilet) return;
    guest_opinions::Lower(g.m_opinion[tables::Variety], def.m_OpinionEffect[tables::Variety][1]);
}

// AlterDesires with a resolved family: every desire named like it is satisfied (no break: the loop runs through all 26, so a name listed twice
// would be cleared twice); Toilet puts the walk and stand animations back to normal; a drink, meal or snack relieves the bladder desire by 0.1 (held at one) unless
// the guest is in the toilet state; the outdoor desires reset the activity. The blocked counter is cleared on every path, also without a building.
inline void AlterDesires(Guest& g, const char* family, const char* const* names)
{
    for (int i = 0; i < kDesireCount; ++i) {
        if (!ascii::EqualsNoCase(family, names[i])) continue;
        g.m_desires[i] = 0.0f;
        if (i == kToilet) {
            std::strcpy(g.m_WalkAnimation, kWalkNormal);
            std::strcpy(g.m_StandAnimation, kStandNormal);
        } else if (i == kAlcohol || i == kMeals || i == kSnacks || i == kSoftDrinks) {
            if (g.m_state != kStateWc) guest_opinions::Raise(g.m_desires[kToilet], kToiletRelief);
            continue;
        }
        if (IsOutdoor(i)) g.m_activity = 0;
    }
    g.m_NumTimesBlocked = 0;
}

// SetupStartValues: the dynamic fields that start from the definition (guest start values, docs/architecture/objects.md).
inline void SetupStartValues(Guest& g, const GuestDef& def)
{
    g.m_drunk = def.m_StartDrunk;
    g.m_desires[kToilet] = def.m_StartBladder;
    g.m_foodIll = def.m_StartIll;
    g.m_sunBurnLevel = def.m_StartSunburn;
    g.m_desires[kVandalise] = def.m_StartVandalise;
    g.m_desires[kFight] = def.m_StartFight;
    g.m_desires[kFlirt] = def.m_StartFlirt;
}

} // namespace obl::guest_desire_actions
