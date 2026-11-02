#pragma once

#include "game/objects/guest_update_opinions.h"

#include <cmath>
#include <cstdint>
#include <optional>

// The pure rules of the small guest functions that pick the look of a guest and report how it looks (guest look, docs/architecture/objects.md):
// RandomPalette, the attached sprites' palette draw and the getters PaletteTone,
// GetFoodIllness, GetSunburnSeverity, IsApproachable and HasState. The game side (sprite instances, the clock,
// the shared rand) is in src/game/objects/guest_appearance.cpp.
//
// 53-bit precision: every operation is one double rounding. "Below" means below or unordered (guest_opinions::Below);
// the other compare is "below, equal or unordered" (a NaN leaves the value alone).
namespace obl::guest_appearance {

using guest_opinions::Below;

// The store rounds to float, but the compare tests the still-unrounded double sum; unordered stays NaN.
inline float AddFoodIll(float current, float amount)
{
    const double sum = static_cast<double>(amount) + static_cast<double>(current);
    return sum > 1.0 ? 1.0f : static_cast<float>(sum);
}

// ---- palettes ---------------------------------------------------------------------------------------------------------------------

// The guest's own palette. The hair group comes from the gender (1 = female: four groups, the first draw masked with 3) or from the sprite name
// (FAT_BLOKE, compared without case: three groups, any other male sprite six); a coloured guest takes the first palette of the group, every
// other one a second draw % 7 plus two into it. The index is clamped to the last palette of the sprite (a signed compare).
// `draw` is the game's rand(), the draws come in that order.
template<class Draw> int32_t RandomPalette(int32_t gender, bool coloured, bool fatBloke, int32_t paletteCount, Draw&& draw)
{
    const int32_t group = gender == 1 ? draw() % 4 : fatBloke ? draw() % 3 : draw() % 6;
    const int32_t index = coloured ? group * 10 + 1 : group * 10 + draw() % 7 + 2;
    return index < paletteCount ? index : paletteCount - 1;
}

// The palette of the sprite attached to the guest (the luggage a leaving guest drags). Luggage follows the guest: when the guest wears its
// last palette the luggage gets its own last one, otherwise a draw % (count - 1) (the last palette is kept for the guest's own colour);
// everything else attached takes a draw % count. The result is clamped like the guest's own (count - 1 for a draw past the end).
// nullopt = a modulus of zero (a divide error): the caller handles that case.
template<class Draw>
std::optional<int32_t> RandomAttachedPalette(bool luggage, int32_t ownIndex, int32_t ownCount, int32_t attachedCount, Draw&& draw)
{
    if (luggage && ownIndex == ownCount - 1) return attachedCount - 1;
    const int32_t modulus = luggage ? attachedCount - 1 : attachedCount;
    if (modulus == 0) return std::nullopt;
    const int32_t index = draw() % modulus;
    return attachedCount <= index ? attachedCount - 1 : index;
}

// ---- getters ----------------------------------------------------------------------------------------------------------------------

constexpr float kPaletteToneAgeScale = 0.013888889f; // 1/72, the guest reaches its full tone after three days
constexpr float kHoursPerDay = 24.0f;

// The tone of the palette the guest is drawn with: a sunburnt guest (SunBurnLevel below zero, NaN too) darkens by 1 - level, at most 2.0; every
// other one fades in with the hours since arrival (the day counter difference as an int times 24 plus the clock difference, over 72), at most 1.0.
inline double PaletteTone(float sunburn, int32_t today, float time, int32_t dayArrived, float timeArrived)
{
    if (Below(sunburn, 0.0f)) {
        const double burnt = 1.0 - static_cast<double>(sunburn);
        return burnt > 2.0 ? 2.0 : burnt;
    }
    const double days = static_cast<double>(today - dayArrived) * static_cast<double>(kHoursPerDay);
    const double hours = (static_cast<double>(time) - static_cast<double>(timeArrived)) + days;
    const double tone = hours * static_cast<double>(kPaletteToneAgeScale);
    return tone > 1.0 ? 1.0 : tone;
}

// How badly the guest is sunburnt, 0..1: the SunBurnLevel is negative while burnt (the sign flip of a NaN stays a NaN).
inline float SunburnSeverity(float sunburn)
{
    if (!Below(sunburn, 0.0f)) return 0.0f;
    const float severity = -sunburn;
    return severity > 1.0f ? 1.0f : severity;
}

// STATE_GUEST_SEARCH_DESIRE (0xc), HEADING_DESIRE (0xd) and the state 0x3a: the states in which a rep or a security guard may talk to the guest.
inline bool IsApproachable(uint32_t state)
{
    return state > 0xb && (state < 0xe || state == 0x3a);
}

// The states the guest's script has a handler for: 0, 1, 5, 0xc..0x13 and 0x15..0x40.
inline bool HasState(uint32_t state)
{
    return state <= 1 || state == 5 || (state >= 0xc && state <= 0x13) || (state >= 0x15 && state <= 0x40);
}

} // namespace obl::guest_appearance
