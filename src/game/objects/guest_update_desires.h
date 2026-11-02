#pragma once

#include "game/objects/object_guest.h"
#include "game/tables/world_params_tables.h"

// UpdateDesires.
// It writes only drunk, the 26 desires, food-ill and the ill flag; it reads the world's frame time
//, the guest's growth rates and its definition record. No RNG, no messages, no allocation.
//
// The arithmetic runs at 53-bit precision: the product of two floats is exact, each statement rounds once to
// double and once on the store to float. The same is spelled out in double here so the result does not depend on the build's
// float evaluation. Comparisons on a fresh result look at the unrounded double (food-ill below zero).
namespace obl {

inline void UpdateGuestDesires(object::Guest& guest, const tables::GuestDef& def, float dt)
{
    auto step = [&](float value, float rate) { return static_cast<double>(value) - static_cast<double>(dt) * rate; };
    auto grow = [&](float value, float rate) { return static_cast<double>(dt) * rate + static_cast<double>(value); };

    guest.m_drunk = static_cast<float>(step(guest.m_drunk, def.m_DrunkDecay));
    // Flirt (20), Fight (19), Vandalise (18); each is a float store followed by a reload for the clamp.
    guest.m_desires[20] = static_cast<float>(step(guest.m_desires[20], def.m_FlirtDecay));
    guest.m_desires[19] = static_cast<float>(step(guest.m_desires[19], def.m_FightDecay));
    guest.m_desires[18] = static_cast<float>(step(guest.m_desires[18], def.m_VandaliseDecay));
    if (guest.m_drunk < 0.0f) guest.m_drunk = 0.0f;
    if (guest.m_desires[20] < 0.0f) guest.m_desires[20] = 0.0f;
    if (guest.m_desires[19] < 0.0f) guest.m_desires[19] = 0.0f;
    if (guest.m_desires[18] < 0.0f) guest.m_desires[18] = 0.0f;

    // Desire index -> growth rate in m_person (the *PREF pairs the guest constructor reads).
    static constexpr int kGrowth[12][2] = {
            {1, 0}, {2, 1}, {5, 2}, {6, 3}, {8, 4}, {9, 5}, {10, 6}, {11, 7}, {12, 8}, {13, 9}, {15, 10}, {16, 11}};
    for (const auto& pair : kGrowth) guest.m_desires[pair[0]] = static_cast<float>(grow(guest.m_desires[pair[0]], guest.m_person[pair[1]]));
    for (float& desire : guest.m_desires)
        if (1.0f < desire) desire = 1.0f;

    if (guest.m_imIll == 0) {
        if (0.99f < guest.m_foodIll) {
            guest.m_imIll = 1;
            guest.m_foodIll = 1.0f;
            return;
        }
        double value = step(guest.m_foodIll, def.m_IllDecay);
        guest.m_foodIll = static_cast<float>(value);
        if (value < 0.0) guest.m_foodIll = 0.0f;
    }
}

// ApplyWeatherModifiers: the active weather's 26 modifiers are added to the guest's desires, one float
// add per desire in double precision (double sum, rounded on the store) and no cap; called for every guest when a weather schedule entry activates.
inline void ApplyGuestWeatherModifiers(object::Guest& guest, const float (&modifiers)[26])
{
    for (int i = 0; i < 26; ++i)
        guest.m_desires[i] = static_cast<float>(static_cast<double>(guest.m_desires[i]) + static_cast<double>(modifiers[i]));
}

} // namespace obl
