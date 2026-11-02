#pragma once

#include "game/objects/object_guest.h"
#include "core/ascii_case.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// UpdateOpinions of the guest, docs/architecture/objects.md. Everything it does to the guest, in a fixed order (tests pin it); the final
// PostDelayed(object, object, MT 0x3f, 2.0 s) is the caller's (it happens for every state, also the four skipped ones).
//
// 53-bit precision: every statement is one double operation rounded once to float on the store, and the clamps
// compare the double that went into the store, not the rounded float. NaN is decided as follows:
// "less than" tests hold for an unordered pair, "greater than" tests do not.
namespace obl {

// What the function reads outside the guest and its definition: the resort values of the World (beauty, litter count) and the
// population statistics the guest constructor/destructor keep (g_GuestAttractionCounts[6] = guests per gender*3 + attractiveness band,
// g_GuestPopulation = all guests).
struct GuestOpinionWorld {
    float scenicValue;
    uint32_t litter;
    const uint32_t* attractionCounts; // 6 entries
    int32_t population;
};

namespace guest_opinions {

inline bool IsLowerClamp(double value)
{
    return !(value >= 0.0);
} // less than or unordered
inline bool IsUpperClamp(double value)
{
    return value > 1.0;
} // strictly greater
// "value is below the limit" with these NaN rules: true for less-than and for an unordered pair.
inline bool Below(double value, double limit)
{
    return !(value >= limit);
}

// opinion -= effect * factor (the fading of an opinion of a guest with no variety), clamped at 0.
inline void Fade(float& opinion, float effect, float factor)
{
    const double value = static_cast<double>(opinion) - static_cast<double>(effect) * static_cast<double>(factor);
    opinion = static_cast<float>(value);
    if (IsLowerClamp(value)) opinion = 0.0f;
}

inline void Raise(float& opinion, float effect)
{
    const double value = static_cast<double>(effect) + static_cast<double>(opinion);
    opinion = static_cast<float>(value);
    if (IsUpperClamp(value)) opinion = 1.0f;
}

inline void Lower(float& opinion, float effect)
{
    const double value = static_cast<double>(opinion) - static_cast<double>(effect);
    opinion = static_cast<float>(value);
    if (IsLowerClamp(value)) opinion = 0.0f;
}

// Band of the guest's preferred attractiveness: < 0.26 (or NaN) = 0, > 0.74 = 2, otherwise 1 (the constants are compared as doubles).
inline int AttractivenessBand(float preference)
{
    const double value = preference;
    if (!(value >= 0.26)) return 0;
    return value > 0.74 ? 2 : 1;
}

// The walk/stand animation pair that goes with a mood (matched by name against the guest's current walk/stand animations).
struct Look {
    const char* walk;
    const char* stand;
    int randModulus; // rand() % modulus - offset + base
    int offset;
    float base;
};
inline constexpr Look kSadLook{"d_walk_sad", "d_stand_sad", 7, 3, 30.0f};
inline constexpr Look kDrunkLook{"d_walk_drunk", "d_stand_drunk", 7, 3, 30.0f};
inline constexpr Look kNormalLook{"d_walk_normal", "d_stand_normal", 15, 7, 60.0f};

inline void Copy(char* destination, const char* source)
{
    std::memcpy(destination, source, std::strlen(source) + 1);
}

} // namespace guest_opinions

// `rand` is the game's rand() (taped in script mode); it is called only when the walk animation changes.
template<class Rand>
void UpdateGuestOpinions(object::Guest& guest, const tables::GuestDef& def, const GuestOpinionWorld& world, Rand&& rand)
{
    using namespace guest_opinions;
    const int state = guest.m_state;
    if (state == 0x28 || state == 0x29 || state == 0x26 || state == 0x25) return;
    float* const opinion = guest.m_opinion;
    using enum tables::GuestOpinion;

    if (opinion[Variety] >= 1e-6f) {
        const float scenic = world.scenicValue;
        if (scenic > guest.m_Tolerance[1])
            Raise(opinion[Beauty], def.m_OpinionEffect[Beauty][0]);
        else if (Below(scenic, guest.m_Tolerance[0]))
            Lower(opinion[Beauty], def.m_OpinionEffect[Beauty][1]);

        // The share of the opposite sex in the preferred attractiveness band (counts kept by the guest constructor, flipped gender index).
        const int index = AttractivenessBand(def.m_AttractivePref) - def.m_Gender * 3;
        const double ratio = static_cast<double>(world.attractionCounts[index + 3]) / static_cast<double>(world.population);
        if (Below(ratio, guest.m_Tolerance[4]))
            Lower(opinion[Totty], def.m_OpinionEffect[Totty][1]);
        else if (ratio > static_cast<double>(guest.m_Tolerance[5]))
            Raise(opinion[Totty], def.m_OpinionEffect[Totty][0]);

        const double litter = static_cast<double>(world.litter);
        if (litter > static_cast<double>(guest.m_Tolerance[11]))
            Lower(opinion[Cleanliness], def.m_OpinionEffect[Cleanliness][1]);
        else if (Below(litter, guest.m_Tolerance[10]))
            Raise(opinion[Cleanliness], def.m_OpinionEffect[Cleanliness][0]);
    } else {
        constexpr float kSlow = 0.083333336f, kSlower = 0.04f;
        Fade(opinion[Beauty], def.m_OpinionEffect[Beauty][1], kSlow);
        Fade(opinion[Totty], def.m_OpinionEffect[Totty][1], kSlow);
        Fade(opinion[Cleanliness], def.m_OpinionEffect[Cleanliness][1], kSlow);
        Fade(opinion[Crowding], def.m_OpinionEffect[Crowding][1], kSlower);
        Fade(opinion[Safety], def.m_OpinionEffect[Safety][1], kSlower);
        Fade(opinion[Price], def.m_OpinionEffect[Price][1], kSlower);
    }

    double sum = 0.0;
    for (int i = 0; i < 7; ++i) sum += static_cast<double>(opinion[i]);
    guest.m_happiness = static_cast<float>(sum * static_cast<double>(0.14285715f));

    // The mood decides the walk and stand animation; nothing changes while the guest already walks that way, and a desperate walk is never replaced.
    if (ascii::EqualsNoCase(guest.m_WalkAnimation, "D_WALK_DESPERATE")) return;
    const bool sad = Below(guest.m_happiness, 0.4f) && Below(guest.m_drunk, 0.5f);
    const bool drunk = !sad && !Below(guest.m_drunk, 0.5f);
    const Look& look = sad ? kSadLook : drunk ? kDrunkLook : kNormalLook;
    if (std::strcmp(guest.m_WalkAnimation, look.walk) == 0) return;
    Copy(guest.m_WalkAnimation, look.walk);
    Copy(guest.m_StandAnimation, look.stand);
    const int roll = static_cast<int>(rand()) % look.randModulus - look.offset;
    const float speed = static_cast<float>(static_cast<double>(roll) + static_cast<double>(look.base));
    guest.m_maxVel = speed;
    guest.m_maxForce = static_cast<float>(static_cast<double>(speed) * static_cast<double>(0.33333334f));
}

} // namespace obl
