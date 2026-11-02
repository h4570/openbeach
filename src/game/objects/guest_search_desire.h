#pragma once

#include "core/ascii_case.h"
#include "core/float_to_int.h"
#include "game/objects/guest_desire_names.h"
#include "game/objects/guest_update_opinions.h"
#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>
#include <string>

// SearchDesire (docs/architecture/objects.md "objects.guest-search-desire"): the guest picks its strongest desire and either finds a building that
// serves it or does it on its own (a stroll, the beach, the sea, a fight, ...). The whole function is a template over an environment so it runs on
// the game (src/game/objects/guest_search_desire.cpp) and in the unit tests:
//   env.Weather()                          the current weather kind (1 = fine)
//   env.GameDeltaTime(), env.DayTime()     the frame time and the hour of the day
//   env.StartStomachIllness(), env.CheckLeaveResort(), env.CheckBedTime()   the three guest function that can take the guest over (true = they did)
//   env.Rand(), env.FindKey(key)           rand() and the text table lookup (-1 = no such text)
//   env.RetryFamilyName()                  name of the commodity family of slot m_desired_commod of building type m_CurTypeID
// env.Provider the function's two static globals {id, slot }
//   env.FindProvider(&slot, &found, &broken, &closed, &beachClosed, &retry, family)   the commodity provider search for this guest, answers the id
//   env.BuildingKey(id), env.CommodityName(id, slot), env.BuildingTypeId(id)          of the building object `id` and its definition
//   env.WalkAlongPath(), env.PlayWalkAnimation(), env.BadVisitSound()                 the pieces of "walking away with nothing"
//   env.FindSunbathePoint(), env.FindSwimPoint(x, y, z), env.RandomWalkPoint()        float arrays owned by the game; FindSwimPoint may answer null
//   env.SouvenirShopAllowed(), env.SetRepeatAction(desire, value), env.Fatal(name), env.Post(message)
//   env.ApplyWalkAnimation()               starts m_WalkAnimation at the guest position and current game time
// Every Post is delivered at once (the guest's own script runs inside it), so guest fields the script may change are read again after one.
//
// 53-bit precision: one double operation per step, one rounding at each float store; a compare that follows a store
// looks at the double. "below" also holds for NaN, "above" does not.
namespace obl::guest_search {

constexpr uint32_t kNoDesire = 26;

// Indices of Guest::m_desires that have a branch of their own.
enum Desire : uint32_t {
    Accommodation = 0,
    Pills = 4,
    PartyFun = 6,
    Souvenir = 10,
    Sunbathe = 12,
    SwimFun = 13,
    Relief = 14,
    WaterFun = 15,
    Stroll = 16,
    Vandal = 18,
    Fight = 19,
    Flirt = 20,
    HardSex = 23,
    Scared = 25,
};

// m_desired_commod while m_activity is set: what the guest does on its own (not a commodity slot).
enum Activity : int32_t { ActStroll = 0, ActSunbathe = 1, ActSwim = 2, ActFight = 3, ActFlirt = 4, ActPee = 6, ActVandal = 8 };

constexpr int32_t kStateHeadingDesire = 0xd;
constexpr int32_t kStateFight = 0x1f;
constexpr int32_t kStateFlirt = 0x22;
constexpr int32_t kStateGoHome = 0x29;
constexpr int32_t kStateVandal = 0x33;
constexpr int32_t kStateNoDesire = 0x39;

constexpr float kReliefThreshold = 0.6f; // the bladder counts as a desire only above this
constexpr float kDesperateRelief = 0.8f; // above this the guest walks desperately
constexpr float kDayStart = 9.0f;
constexpr float kDayEnd = 18.0f;
constexpr float kMoodIconSeconds = 2.0f;
constexpr int32_t kBadWeather = 4; // heavy rain and thunderstorm: no stroll, no swim, no party
constexpr int32_t kWaterFunWeather = 2; // water fun needs weather 0..2
constexpr int32_t kWalkingPerson = 11; // m_person[11]: positive for a guest that may be told to carry on without a desire
constexpr int32_t kMoodIconHappy = 0, kMoodIconSad = 1;

constexpr const char* kThoughtMissed = "GUEST_THOUGHT_MISSED_";
constexpr const char* kThoughtBeachClosed = "GUEST_THOUGHT_BEACH_CLOSED_";
constexpr const char* kThoughtBroken = "GUEST_THOUGHT_BROKEN_";
constexpr const char* kThoughtClosed = "GUEST_THOUGHT_CLOSED_";
constexpr const char* kThoughtHead = "GUEST_THOUGHT_HEAD_";
constexpr const char* kThoughtGoHome = "GUEST_THOUGHT_GO_HOME";
constexpr const char* kThoughtHeadacheLater = "GUEST_THOUGHT_HEADACHE_LATER";
constexpr const char* kThoughtVandal = "GUEST_THOUGHT_HEAD_ACT_VANDALISING";
constexpr const char* kThoughtFlirt = "GUEST_THOUGHT_HEAD_ACT_FLIRTING";
constexpr const char* kDesperateWalk = "D_WALK_DESPERATE";
constexpr const char* kNormalWalk = "D_WALK_NORMAL";
constexpr const char* kNormalStand = "D_STAND_NORMAL";
constexpr const char* kDesperateStandOdd = "D_STAND_DESPERATE1";
constexpr const char* kDesperateStandEven = "D_STAND_DESPERATE2";

// The two static globals of the function: the provider FindProvider answered last and the commodity slot it wrote through the pointer.
struct Provider {
    int32_t id;
    int32_t slot;
};

// The refusal counters FindProvider fills: it clears `broken` itself, the other two only count up.
struct Refusals {
    int32_t broken = 0, closed = 0, beachClosed = 0;
    uint32_t Total() const { return static_cast<uint32_t>(beachClosed) + static_cast<uint32_t>(closed) + static_cast<uint32_t>(broken); }
};

// The thought of the weather the guest notices on every search; fine weather (1) has none and an unknown kind leaves the buffer unwritten.
inline const char* WeatherThought(int32_t weather)
{
    switch (weather) {
    case 0: return "GUEST_THOUGHT_WEATHER_HEAT_WAVE";
    case 2: return "GUEST_THOUGHT_WEATHER_OVERCAST";
    case 3: return "GUEST_THOUGHT_WEATHER_LIGHTRAIN";
    case 4: return "GUEST_THOUGHT_WEATHER_HEAVYRAIN";
    case 5: return "GUEST_THOUGHT_WEATHER_THUNDERSTORM";
    default: return "";
    }
}

// The thought of setting off for an activity; codes 5, 7 and above 8 leave the buffer unwritten (no activity uses them).
inline const char* ActivityThought(int32_t activity)
{
    switch (activity) {
    case ActStroll: return "GUEST_THOUGHT_HEAD_ACT_STROLL";
    case ActSunbathe: return "GUEST_THOUGHT_HEAD_ACT_SUNBATHE";
    case ActSwim: return "GUEST_THOUGHT_HEAD_ACT_SWIMMING";
    case ActFight: return "GUEST_THOUGHT_HEAD_ACT_FIGHTING";
    case ActFlirt: return kThoughtFlirt;
    case ActPee: return "GUEST_THOUGHT_HEAD_ACT_PEEING";
    case ActVandal: return kThoughtVandal;
    default: return "";
    }
}

// Between nine and six; a NaN hour is not daytime.
inline bool Daytime(float hour)
{
    return hour < kDayEnd && hour > kDayStart;
}

// The strongest desire of the copy: strictly above the best so far (or unordered with it); the bladder only counts above its threshold.
inline void PickStrongest(const float* desires, float& best, uint32_t& desire)
{
    for (uint32_t i = 0; i < kDesireCount; ++i) {
        if (!guest_opinions::Below(best, desires[i])) continue;
        if (i == Relief && !(desires[Relief] > kReliefThreshold)) continue;
        desire = i;
        best = desires[i];
    }
}

// The desire whose family name is `family`, or kNoDesire.
inline uint32_t DesireOfFamily(const char* family)
{
    for (uint32_t i = 0; i < kDesireCount; ++i)
        if (ascii::EqualsNoCase(family, kDesireNames[i])) return i;
    return kNoDesire;
}

// How often the activity repeats: MAX_<desire> (read as unsigned) times the desire, truncated; 0 becomes 1 (an unsigned test, negatives stay).
inline int32_t RepeatCount(int32_t maximum, float best)
{
    const int32_t count = FloatToIntWrap(static_cast<double>(static_cast<uint32_t>(maximum)) * static_cast<double>(best));
    return static_cast<uint32_t>(count) < 1 ? 1 : count;
}

inline void PushThought(object::Guest& guest, int32_t id)
{
    if (id == -1) return;
    guest.m_Thoughts[2] = guest.m_Thoughts[1];
    guest.m_Thoughts[1] = guest.m_Thoughts[0];
    guest.m_Thoughts[0] = id;
}

// Every refused provider costs the guest's opinion of the variety one step; the floor is tested on the double.
inline void VarietyPenalty(object::Guest& guest, uint32_t refusals)
{
    float& variety = guest.m_opinion[tables::Variety];
    const double value = static_cast<double>(variety) -
            static_cast<double>(refusals) * static_cast<double>(guest.m_definition->m_OpinionEffect[tables::Variety][1]);
    variety = static_cast<float>(value);
    if (guest_opinions::IsLowerClamp(value)) variety = 0.0f;
}

// 0..1 on the stored float.
inline void ClampOpinion(float& opinion)
{
    if (guest_opinions::IsLowerClamp(opinion))
        opinion = 0.0f;
    else if (guest_opinions::IsUpperClamp(opinion))
        opinion = 1.0f;
}

// The mood icon after a visit: a good last visit goes out and shows the happy icon, otherwise a bad one goes out and shows the sad one; the icon
// changes only when none is showing.
inline void SettleVisitMood(object::Guest& guest)
{
    int32_t icon;
    if (guest.m_lastGood) {
        guest.m_lastGood = 0;
        icon = kMoodIconHappy;
    } else if (guest.m_lastBad) {
        guest.m_lastBad = 0;
        icon = kMoodIconSad;
    } else {
        return;
    }
    if (guest.m_moodIconTime > 0.0f) return;
    guest.m_moodIcon->m_frameIndex = icon;
    guest.m_moodIconTime = kMoodIconSeconds;
}

inline messages::Message Own(const object::Guest& guest, int32_t type, int32_t flags, int32_t data)
{
    messages::Message message{}; // the words past data stay zero; nobody reads them
    message.sender = static_cast<int>(guest.m_id);
    message.receiver = static_cast<int>(guest.m_id);
    message.type = type;
    message.flags = flags;
    message.data = data;
    return message;
}

namespace detail {

inline void Copy(char* destination, const char* source)
{
    std::memcpy(destination, source, std::strlen(source) + 1);
}

// prefix + middle + (rand() % 3 + 1), looked up and pushed when the text exists. The digit follows without a separator.
template<class Env> void Thought(object::Guest& guest, Env& env, const std::string& stem)
{
    const std::string key = stem + std::to_string(env.Rand() % 3 + 1);
    PushThought(guest, env.FindKey(key.c_str()));
}

// Why no provider took the guest, by the first counter that is set: closed, broken, beach closed, or simply nothing there.
template<class Env> void RefusalThought(object::Guest& guest, Env& env, const Refusals& refusals, const Provider& provider, uint32_t desire)
{
    std::string stem;
    if (refusals.closed != 0)
        stem = std::string(kThoughtClosed) + env.BuildingKey(provider.id);
    else if (refusals.broken != 0)
        stem = std::string(kThoughtBroken) + env.BuildingKey(provider.id);
    else if (refusals.beachClosed != 0)
        stem = std::string(kThoughtBeachClosed) + env.CommodityName(provider.id, provider.slot);
    else
        stem = std::string(kThoughtMissed) + kDesireNames[desire];
    Thought(guest, env, stem);
}

// The guest gives up on the desire and walks on: a random stretch of path, the walk animation, the sad icon and sound.
template<class Env> void WalkAway(object::Guest& guest, Env& env)
{
    env.WalkAlongPath();
    guest.m_missedPurchase = 1;
    guest.m_lastBad = 1;
    env.PlayWalkAnimation();
    SettleVisitMood(guest);
    env.BadVisitSound();
}

template<class Env> void GoHome(object::Guest& guest, Env& env)
{
    Thought(guest, env, kThoughtGoHome);
    env.Post(Own(guest, messages::MT_STATECHANGE, 0, kStateGoHome));
}

inline void SetPoint(float* point, float x, float y)
{
    point[0] = x;
    point[1] = y;
}

} // namespace detail

// SearchDesire. True: the guest has something to do (or was taken over); false only for a frame without game time.
template<class Env> bool SearchDesire(object::Guest& guest, Env& env)
{
    using namespace detail;
    uint8_t found = 0, retry = 0;
    bool missed = false, fight = false;
    Refusals refusals;
    Provider& provider = env.Provider();
    const tables::GuestDef& def = *guest.m_definition;

    guest.m_HasSatDown = 0;
    const int32_t weather = env.Weather();
    guest.m_missedDesireLast = guest.m_missedDesire != 0 ? 1 : 0;
    if (!(env.GameDeltaTime() > 0.0f)) return false;
    if (env.StartStomachIllness()) return true;
    if (env.CheckLeaveResort()) return true;
    float desires[kDesireCount];
    std::memcpy(desires, guest.m_desires, sizeof(desires));
    if (env.CheckBedTime()) return true;

    if (const int32_t now = env.Weather(); now != 1) Thought(guest, env, WeatherThought(now));

    float best = 0.0f;
    uint32_t desire = kNoDesire;
    const auto findProvider = [&](uint32_t family) {
        provider.id = env.FindProvider(
                &provider.slot, &found, &refusals.broken, &refusals.closed, &refusals.beachClosed, &retry, static_cast<int32_t>(family));
    };

    while (!found) {
        best = 0.0f;
        refusals = {};
        desire = kNoDesire;
        if (guest.m_missedDesire != 0 && guest.m_activity == 0 && guest.m_CurID != guest.m_StayingAt) {
            // The last building failed the guest once: try the same family again. A hit keeps `best` at 0; family 0 (the test is on the index, not on
            // "none") falls back to the strongest desire.
            desire = DesireOfFamily(env.RetryFamilyName());
            if (desire == Accommodation) PickStrongest(desires, best, desire);
            guest.m_missedDesire = 0;
        } else {
            guest.m_activity = 0;
            PickStrongest(desires, best, desire);
        }
        if (desire == kNoDesire) {
            if (guest.m_person[kWalkingPerson] > 0.0f)
                env.Post(Own(guest, messages::MT_STATECHANGE, 0, kStateNoDesire));
            else
                env.Fatal(guest.m_Name);
            return true;
        }
        desires[desire] = 0.0f;

        if (desire == Stroll) {
            if (weather >= kBadWeather) continue;
            const float* point = env.RandomWalkPoint();
            SetPoint(guest.m_walkpoints[0], point[0], point[1]);
            guest.m_desired_commod = ActStroll;
            guest.m_walkpoint = 0;
            guest.m_activity = 1;
            found = 1;
            guest.m_RepeatAction = RepeatCount(def.m_Max[Stroll], best);
            guest.m_DestObjectID = -1;
        } else if (desire == Sunbathe) {
            if (!Daytime(env.DayTime()) || (weather != 1 && weather != 0)) continue;
            findProvider(Sunbathe);
            if (const uint32_t total = refusals.Total(); total != 0) VarietyPenalty(guest, total);
            // Without a lounger to rent, and one time in four with one, the guest lies down on the beach itself.
            if (!found || (env.Rand() % 4 == 0 && Daytime(env.DayTime()))) {
                const float* point = env.FindSunbathePoint();
                SetPoint(guest.m_walkpoints[0], point[3], point[4]);
                found = 1;
                guest.m_activity = 1;
                SetPoint(guest.m_walkpoints[1], 0.0f, 0.0f);
                SetPoint(guest.m_walkpoints[2], 0.0f, 0.0f);
                guest.m_desired_commod = ActSunbathe;
                guest.m_walkpoint = 0;
                guest.m_DestObjectID = -1;
            }
            guest.m_RepeatAction = RepeatCount(def.m_Max[Sunbathe], best);
        } else if (desire == SwimFun) {
            if (weather >= kBadWeather) continue;
            findProvider(SwimFun);
            if (!found) {
                if (const uint32_t total = refusals.Total(); total != 0) {
                    VarietyPenalty(guest, total);
                    if (!retry) {
                        RefusalThought(guest, env, refusals, provider, SwimFun);
                        missed = true;
                        WalkAway(guest, env);
                        found = 1;
                        float& variety = guest.m_opinion[tables::Variety];
                        const double value = static_cast<double>(variety) - static_cast<double>(def.m_OpinionEffect[tables::Variety][1]);
                        variety = static_cast<float>(value);
                        if (guest_opinions::IsLowerClamp(value))
                            variety = 0.0f;
                        else if (guest_opinions::IsUpperClamp(value))
                            variety = 1.0f;
                        guest.m_desires[SwimFun] = 0.0f;
                        continue;
                    }
                }
            } else if (env.Rand() % 4 != 0 || !Daytime(env.DayTime())) {
                continue; // three times in four, and always outside the day, the pool it found will do
            }
            if (!Daytime(env.DayTime())) continue;
            found = 0;
            if (const float* point = env.FindSwimPoint(guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2])) {
                SetPoint(guest.m_walkpoints[0], point[0], point[1]);
                SetPoint(guest.m_walkpoints[1], point[3], point[4]);
                SetPoint(guest.m_walkpoints[2], 0.0f, 0.0f);
                guest.m_walkpoint = 0;
                guest.m_DestObjectID = -1;
                guest.m_activity = 1;
                guest.m_desired_commod = ActSwim;
                found = 1;
                guest.m_RepeatAction = RepeatCount(def.m_Max[SwimFun], best);
            } else {
                Thought(guest, env, std::string(kThoughtMissed) + kDesireNames[SwimFun]);
                missed = true;
                WalkAway(guest, env);
                found = 1;
                // No sea to swim in costs a step of every opinion; only the floor is applied here.
                for (int i = 0; i < 7; ++i)
                    guest.m_opinion[i] =
                            static_cast<float>(static_cast<double>(guest.m_opinion[i]) - static_cast<double>(def.m_OpinionEffect[i][1]));
                guest.m_desires[SwimFun] = 0.0f;
                for (float& opinion : guest.m_opinion)
                    if (guest_opinions::IsLowerClamp(opinion)) opinion = 0.0f;
            }
        } else if (desire == Fight) {
            guest.m_desired_commod = ActFight;
            guest.m_activity = 1;
            guest.m_DestObjectID = -1;
            fight = true;
            found = 1;
            env.Post(Own(guest, messages::MT_STATECHANGE, 0, kStateFight));
        } else if (desire == Vandal) {
            guest.m_desired_commod = ActVandal;
            guest.m_activity = 1;
            guest.m_DestObjectID = -1;
            env.Post(Own(guest, messages::MT_STATECHANGE, 0, kStateVandal));
            Thought(guest, env, kThoughtVandal);
            return true;
        } else if (desire == Scared || desire == HardSex) {
            guest.m_desires[desire] = 0.0f;
        } else if (desire == Flirt) {
            guest.m_activity = 1;
            guest.m_desired_commod = ActFlirt;
            env.Post(Own(guest, messages::MT_STATECHANGE, 0, kStateFlirt));
            Thought(guest, env, kThoughtFlirt);
            return true;
        } else {
            if ((desire == WaterFun && weather > kWaterFunWeather) || (desire == PartyFun && weather >= kBadWeather)) {
                // Not in this weather: as if every provider had asked the guest to come back later.
                found = 0;
                retry = 1;
                refusals = {};
            } else {
                findProvider(desire);
                if (static_cast<uint32_t>(refusals.closed) + 2u * static_cast<uint32_t>(refusals.beachClosed) +
                                static_cast<uint32_t>(refusals.broken) !=
                        0)
                    VarietyPenalty(guest, refusals.Total());
                if (found) {
                    env.SetRepeatAction(static_cast<int32_t>(desire), best);
                    continue;
                }
            }
            if (desire == Relief) {
                // No toilet: here and now.
                found = 1;
                guest.m_activity = 1;
                guest.m_desired_commod = ActPee;
                guest.m_walkpoint = 0;
                for (int axis = 0; axis < 3; ++axis) guest.m_walkpoints[0][axis] = guest.m_walkpoints[1][axis] = guest.m_WorldXYZ[axis];
                guest.m_DestObjectID = -1;
                continue;
            }
            if (desire == Souvenir && !env.SouvenirShopAllowed()) {
                GoHome(guest, env);
                return true;
            }
            if (!retry) {
                RefusalThought(guest, env, refusals, provider, desire);
                missed = true;
                WalkAway(guest, env);
                found = 1;
                float& variety = guest.m_opinion[tables::Variety];
                variety = static_cast<float>(static_cast<double>(variety) - static_cast<double>(def.m_OpinionEffect[tables::Variety][1]));
                ClampOpinion(variety);
                if (desire == Pills) {
                    guest.m_desires[Pills] = 0.0f;
                    continue;
                }
            } else {
                if (desire == Pills) Thought(guest, env, kThoughtHeadacheLater);
                retry = 0;
            }
            if (desire == Souvenir) {
                GoHome(guest, env);
                return true;
            }
            if (desire != Pills) guest.m_desires[desire] = 0.0f;
        }
    }

    if (missed) return true;
    if (!fight) env.Post(Own(guest, messages::MT_STATECHANGE, 0, kStateHeadingDesire));

    const auto remember = [&](uint8_t activity, int32_t what) {
        guest.m_lastActivity[guest.m_ArrayPos] = activity;
        guest.m_LastID[guest.m_ArrayPos] = what;
        guest.m_ArrayPos = static_cast<int32_t>((static_cast<uint32_t>(guest.m_ArrayPos) - 1u) & 1u);
    };
    const auto raiseVariety = [&] {
        float& variety = guest.m_opinion[tables::Variety];
        variety = static_cast<float>(static_cast<double>(def.m_OpinionEffect[tables::Variety][0]) + static_cast<double>(variety));
    };
    if (guest.m_activity == 0) {
        remember(0, provider.id);
        guest.m_CurID = provider.id;
        guest.m_CurTypeID = env.BuildingTypeId(provider.id);
        guest.m_desired_commod = provider.slot;
        env.Post(Own(guest, messages::MT_PAVEMENT2OBJECT, 1, provider.id));
        Thought(guest, env, std::string(kThoughtHead) + env.CommodityName(guest.m_CurID, guest.m_desired_commod));
        if (desire != Flirt && desire != Fight) raiseVariety();
    } else {
        Thought(guest, env, ActivityThought(guest.m_desired_commod));
        remember(1, guest.m_desired_commod);
        if (guest.m_desired_commod == ActSwim || guest.m_desired_commod == ActPee || !fight)
            env.Post(Own(guest, messages::MT_PAVEMENT2POINT, FloatToIntWrap(guest.m_walkpoints[0][0]),
                    FloatToIntWrap(guest.m_walkpoints[0][1])));
        // Doing something on one's own is no proof of variety.
        if (desire != Flirt && desire != Fight && desire != Relief && desire != Stroll && desire != SwimFun && desire != Sunbathe)
            raiseVariety();
    }

    if (desire == Relief && best > kDesperateRelief) {
        Copy(guest.m_WalkAnimation, kDesperateWalk);
        Copy(guest.m_StandAnimation, env.Rand() % 2 == 0 ? kDesperateStandEven : kDesperateStandOdd);
    } else if (std::strcmp(guest.m_WalkAnimation, kDesperateWalk) == 0) {
        Copy(guest.m_WalkAnimation, kNormalWalk);
        Copy(guest.m_StandAnimation, kNormalStand);
    }
    env.ApplyWalkAnimation();
    Copy(guest.m_CurrAnimation, guest.m_WalkAnimation);
    ClampOpinion(guest.m_opinion[tables::Variety]);
    return found != 0;
}

} // namespace obl::guest_search
