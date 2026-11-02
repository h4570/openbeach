#pragma once

#include "core/char16.h"
#include "game/objects/guest_lifecycle.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"
#include "game/tables/world_params_tables.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Construct (GuestCreateParams*, returns this), guest constructor, docs/architecture/objects.md. Pure logic: whatever leaves
// the object goes through an `Env` that the game side maps onto the game's services (src/game/objects/guest_lifecycle_driver.cpp) and the unit test
// onto a fake. The order of the draws and of the effects is fixed, so the RNG tape and the heap always see the same sequence.
//
// Env:
//   const GuestDef* Definition(const void* params)                guest key lookup + guest definition lookup; stores g_GuestConstructDefinition
//   int Rand()                                                    the game's rand
//   float GameTime() / int32_t GameDay() / float DayTime()        elapsed seconds, zero-based day and fractional hour
//   int32_t* Counts()  int32_t& Population()                      g_GuestAttractionCounts[6], g_GuestPopulation
// void* ScriptLookup(const char*) the object script lookup
//   uint32_t NameCount(int list)  const char* NameKey(int list, uint32_t index)   WorldParams::m_nameCounts / m_names; list 0 male, 1 female, 2 surname
//   void* New(size_t)  void Delete(void*)  void Widen(const char*, wchar_t*, size_t)  const wchar_t* Lookup(const wchar_t*)
// the game heap, MultiByteToWideChar(CP_ACP), wide text lookup on g_TextTable
// void* CreateSprite(const char* name) allocates a new sprite instance using the global sprite bank
namespace obl::guest::construct {

using object::Guest;
using tables::GuestDef;

// The draw that places a value in a definition's (min, max) pair: ((max - min) * (draw * 3.0518509e-5f)) + min, every operation one rounding to
// 53 bits, one float store. The pair is not sorted.
inline float Lerp(float min, float max, int32_t draw)
{
    const double scaled = static_cast<double>(draw) * static_cast<double>(3.0518509447574615e-05f);
    const double span = static_cast<double>(max) - static_cast<double>(min);
    return static_cast<float>(span * scaled + static_cast<double>(min));
}

inline void CopyBits(float& destination, const float& source)
{
    std::memcpy(&destination, &source, sizeof(float));
}
// The slot of g_GuestAttractionCounts the constructor counts the guest in: the band of `m_Attractive` (below 0.26 or NaN = 0, up to 0.74 = 1, above = 2;
// the limits are the doubles) plus three per gender. The destructor takes it back with other limits (guest_lifecycle::AttractionSlot:
// 0.2/0.7), so a guest between them leaves a different slot than it entered.
inline int32_t ConstructionSlot(float attractive, int32_t gender)
{
    const double value = attractive;
    const int band = guest_lifecycle::Below(value, 0.26) ? 0 : (value > 0.74 ? 2 : 1);
    return band + gender * 3;
}

// The hour drawn from a (first, second) pair of the sleep or wake window. A window that wraps past midnight (first above second, NaN does not) draws
// from (second + 24 - first) and may land at 24 or later; the bed time folds that back at once, comparing the unrounded double (`fold`).
inline float DrawHour(float first, float second, int32_t draw, bool fold)
{
    if (!(first > second)) return Lerp(first, second, draw);
    const double scaled = static_cast<double>(draw) * static_cast<double>(3.0518509447574615e-05f);
    const double value = ((static_cast<double>(second) + 24.0) - static_cast<double>(first)) * scaled + static_cast<double>(first);
    if (fold && !guest_lifecycle::Below(value, 24.0)) return static_cast<float>(value - 24.0);
    return static_cast<float>(value);
}

// The display name: "<first> <family>" in a buffer that Destruct gives back. Both keys are drawn first (unsigned remainder of the list size), each
// widened into its own temporary block of the game heap; both texts are looked up, the temporaries freed in a fixed order, and the name
// block holds (first + family + 2) characters.
template<class Env> Char16* BuildName(const GuestDef& def, Env& env)
{
    const int list = def.m_Gender == 0 ? 0 : 1;
    auto widen = [&](int kind, Char16** block) {
        const uint32_t pick = static_cast<uint32_t>(env.Rand()) % env.NameCount(kind);
        const char* key = env.NameKey(kind, pick);
        const size_t length = std::strlen(key) + 1;
        *block = static_cast<Char16*>(env.New(length * sizeof(Char16)));
        env.Widen(key, *block, length);
    };
    Char16* firstKey = nullptr;
    Char16* familyKey = nullptr;
    widen(list, &firstKey);
    widen(2, &familyKey);
    const Char16* first = env.Lookup(firstKey);
    const Char16* family = env.Lookup(familyKey);
    env.Delete(firstKey);
    env.Delete(familyKey);
    size_t firstLength = 0, familyLength = 0;
    while (first[firstLength]) ++firstLength;
    while (family[familyLength]) ++familyLength;
    auto* name = static_cast<Char16*>(env.New((familyLength + firstLength + 2) * sizeof(Char16)));
    std::memcpy(name, first, firstLength * sizeof(Char16));
    name[firstLength] = u' ';
    std::memcpy(name + firstLength + 1, family, (familyLength + 1) * sizeof(Char16));
    return name;
}

// Guest's constructor runs it after Person's (whose Mover part draws once).
template<class Env> Guest* Construct(Guest& guest, const void* params, Env& env)
{
    const GuestDef* def = env.Definition(params);
    guest.m_definition = def;

    // Growth rates: one draw per (min, max) pair, in the order of m_person.
    for (int i = 0; i < 13; ++i) guest.m_person[i] = Lerp(def->m_Pref[i][0], def->m_Pref[i][1], env.Rand());
    // The tolerances are the definition's pairs as they are (bit copies).
    std::memcpy(guest.m_Tolerance, def->m_OpinionTol.data(), sizeof(guest.m_Tolerance));
    // Starting opinions; the order of the draws is the compiler's: cleanliness, crowding, totty, price, variety, safety, beauty.
    for (int index : {5, 1, 2, 3, 4, 6, 0})
        guest.m_opinion[index] = Lerp(def->m_OpinionStart[index][0], def->m_OpinionStart[index][1], env.Rand());

    guest.m_lastGood = 0;
    guest.m_lastBad = 0;
    guest.m_souvenirsDecided = 0;
    guest.m_missedDesireLast = 0;
    guest.m_missedDesire = 0;
    guest.m_missedPurchase = 1;
    // Happiness starts as the mean of the opinions: seven additions in 53 bits from zero, times the float 1/7, one float store. (A sprintf of the
    // opinions into a local buffer, a leftover "A guest has got tooooo happy" warning, has no effect.)
    double total = 0.0;
    for (float opinion : guest.m_opinion) total += opinion;
    guest.m_happiness = static_cast<float>(total * static_cast<double>(0.14285715f));

    // Nights of the stay: min + draw % (max - min + 1), unsigned (a definition with max below min divides by zero).
    const uint32_t nightSpan = static_cast<uint32_t>(def->m_NightsMax) - static_cast<uint32_t>(def->m_NightsMin) + 1u;
    guest.m_NightsToStay = static_cast<int32_t>(static_cast<uint32_t>(env.Rand()) % nightSpan + def->m_NightsMin);
    guest.m_DayArrived = env.GameDay();

    // The sleep window: bed time from the SLEEP pair, wake time from the WAKE pair. A bed window wrapping past midnight folds a result of 24 or later;
    // a wrapping wake window checks the BED time (not its own result) against 24 and folds that; tests pin this.
    guest.m_BedTime = DrawHour(def->m_SleepTime[0], def->m_SleepTime[1], env.Rand(), true);
    const bool wakeWraps = def->m_WakeTime[0] > def->m_WakeTime[1];
    guest.m_WakeTime = DrawHour(def->m_WakeTime[0], def->m_WakeTime[1], env.Rand(), false);
    if (wakeWraps && !guest_lifecycle::Below(guest.m_BedTime, 24.0))
        guest.m_BedTime = static_cast<float>(static_cast<double>(guest.m_BedTime) - 24.0);

    // Population statistics (Destruct undoes them with other limits, see ConstructionSlot).
    ++env.Population();
    ++env.Counts()[ConstructionSlot(def->m_Attractive, def->m_Gender)];
    guest.m_script = static_cast<script::ScriptProgram*>(env.ScriptLookup("GuestLogic"));

    // Desires start at zero but the sentinel of SunTanLotion (docs/architecture/objects.md).
    for (float& desire : guest.m_desires) desire = 0.0f;
    guest.m_desires[22] = -1.0f;
    guest.m_waitingForDeath = 0;
    guest.m_rides_start = 0;
    guest.m_rides_end = 0;
    guest.m_drunk = 0.0f;
    guest.m_oxygen = 100;
    CopyBits(guest.m_cash, def->m_Money);
    guest.m_attachedSprite = nullptr;
    guest.m_ReturnAnimation[0] = 0;
    guest.m_TimeToLeave = 0.0f;
    guest.m_topless = 0;

    guest.m_name = BuildName(*def, env);
    guest.m_activity = 0;
    // Only the text and its terminator are written; the rest of the 40 bytes stays as the allocation had it.
    std::memcpy(guest.m_WalkAnimation, "D_WALK_NORMAL", sizeof("D_WALK_NORMAL"));
    std::memcpy(guest.m_StandAnimation, "D_STAND_NORMAL", sizeof("D_STAND_NORMAL"));
    guest.m_Fidget = 0;
    guest.m_currentMessage = nullptr;
    guest.m_walkpoint = 0;
    guest.m_AttachedOffsetX = 0;
    guest.m_AttachedOffsetY = 0;
    guest.m_DestObjectID = -1;
    guest.m_justLittered = 0;
    guest.m_timeLittered = 0.0f;
    guest.m_timeOfOffence = 0.0f;
    guest.m_FightPerson = -1;
    guest.m_NumTimesBlocked = 0;
    guest.m_AmountLoition = 0;
    CopyBits(guest.m_sunBurnLevel, def->m_SunburnTime);
    guest.m_StayingAt = -1;
    guest.m_ArrayPos = 0;
    guest.m_AppliedLotion = 0;
    guest.m_BestSpotOpinion = 0;
    guest.m_bestScore = 0.0f;
    guest.m_BestType = -1;
    guest.m_WorstSpotOpinion = 0;
    guest.m_worstScore = 0.0f;
    guest.m_WorstType = -1;
    guest.m_curScore = 0.0f;
    guest.m_priceScore = 0.0f;
    guest.m_queueScore = 0.0f;
    guest.m_safetyScore = 0.0f;
    guest.m_cleanScore = 0.0f;
    guest.m_foodIll = 0.0f;
    guest.m_imIll = 0;
    guest.m_Best = -2;
    guest.m_Worst = -2;
    guest.m_OffenceState = 0;
    guest.m_timeArrived = env.DayTime();
    for (int32_t& thought : guest.m_Thoughts) thought = -1;

    // The two sprites of the guest's HUD: the smile (no reader) and the thought bubble (the mood icon), both in animation type 3.
    auto* smile = static_cast<ui::SpriteInstance*>(env.CreateSprite("UI3_hud_smile"));
    guest.m_happinessSprite = smile;
    smile->m_animType = 3;
    auto* bubble = static_cast<ui::SpriteInstance*>(env.CreateSprite("UI3_thoughtbubble"));
    guest.m_moodIcon = bubble;
    bubble->m_animType = 3;
    guest.m_moodIconTime = 0.0f;
    guest.m_leavingEarly = 0;
    guest.m_lastUpdateHappened = env.GameTime();
    return &guest;
}

} // namespace obl::guest::construct
