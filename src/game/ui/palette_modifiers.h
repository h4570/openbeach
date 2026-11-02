#pragma once

#include <cstddef>
#include <cstdint>
#include "formats/palette_modifiers_pack.h"
#include "game/ui/sprite.h"

namespace obl::palette_modifiers {

// The engine's toning of a guest palette from the pack's tables (formats/palette_modifiers_pack.h).
struct Object {
    uint16_t tables[kTableCount * kTableEntries];
    uint16_t result[kTableEntries]; // the result palette: 64 entries after the twelve tables
};
inline constexpr size_t kObjectBytes = sizeof(Object);

struct Range {
    int first, last; // inclusive palette entries
};

struct Plan {
    bool valid = false; // false: no modifier id, the function returns null without touching the result
    int lowTable = 0; // table number (0-11) the first colour comes from
    int withTable = 0; // table number the second colour comes from
    float weight = 0.0f; // weight argument of color_mix::Mix: 1 - (blend, or scale - 1, or scale)
    int rangeCount = 0; // palette entries [first, last] that are mixed (the others keep the sprite palette)
    Range ranges[4] = {};
};

// `extra` is the low byte of BuildTonedPaletteWithExtra's fifth argument (nonzero also mixes the final range of ids 3 and 4); the plain
// BuildTonedPalette is `extra = false`.
Plan MakePlan(int modifierId, int variant, float scale, float blend, bool extra);

// Applies a plan: result[i] = mix(tables[lowTable * 64 + i], tables[withTable * 64 + i], weight) for every planned entry.
template<class Mix> void Apply(const Plan& plan, const uint16_t* tables, uint16_t* result, Mix mix)
{
    for (int r = 0; r < plan.rangeCount; ++r)
        for (int i = plan.ranges[r].first; i <= plan.ranges[r].last; ++i)
            result[i] = static_cast<uint16_t>(
                    mix(tables[plan.lowTable * kTableEntries + i], tables[plan.withTable * kTableEntries + i], plan.weight));
}

} // namespace obl::palette_modifiers

namespace obl::ui {
// The live palette modifiers is independent of the static loading gradient.
void* CurrentPaletteModifiers();
void SetPaletteModifiers(void* modifiers);
void* ConstructPaletteModifiers(void* modifiers);
const uint16_t* BuildTonedPalette(void* modifiers, ui::SpriteInstance* instance, float scale, float blend);
const uint16_t* BuildTonedPaletteWithExtra(void* modifiers, ui::SpriteInstance* instance, float scale, float blend, uint32_t extra);

bool RegisterPaletteDrivers();
} // namespace obl::ui
