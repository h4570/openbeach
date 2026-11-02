#include "game/ui/palette_modifiers.h"

namespace obl::palette_modifiers {
namespace {

// Entries of the groups that every id mixes; named by what they colour (the palette's 1-24 are the body, the 32-63 blocks the outfits).
constexpr Range kBody{1, 24};
constexpr Range kBlock32{32, 39};
constexpr Range kBlock40{40, 47};
constexpr Range kBlock48{48, 55};
constexpr Range kBlock56{56, 63};

// FLT_EPSILON (the float): `scale - 1` above it selects the next pair of tables, a `blend` above it replaces the weight.
constexpr double kEpsilon = 1.1920929e-07;

void Add(Plan* plan, Range range)
{
    plan->ranges[plan->rangeCount++] = range;
}

// id 1 (variants 0-2), id 2 (variants 0-5) and ids 3 and 4 (variants 0-3, the last block of each only with `extra`).
void PickRanges(Plan* plan, int id, int variant, bool extra)
{
    if (id == 1) {
        if (variant >= 0 && variant <= 2) {
            Add(plan, kBody);
            if (variant == 0) Add(plan, kBlock40);
            if (variant <= 1) Add(plan, kBlock32);
        }
    } else if (id == 2) {
        switch (variant) {
        case 0:
            Add(plan, kBody);
            Add(plan, kBlock48);
            Add(plan, kBlock56);
            Add(plan, kBlock32);
            break;
        case 1:
            Add(plan, kBody);
            Add(plan, kBlock56);
            Add(plan, kBlock32);
            break;
        case 2:
            Add(plan, kBody);
            Add(plan, kBlock32);
            break;
        case 3:
            Add(plan, kBody);
            Add(plan, kBlock48);
            Add(plan, kBlock56);
            break;
        case 4:
            Add(plan, kBody);
            Add(plan, kBlock56);
            break;
        case 5: Add(plan, kBody); break;
        default: break;
        }
    } else {
        switch (variant) {
        case 0:
            Add(plan, kBody);
            Add(plan, kBlock32);
            Add(plan, kBlock48);
            if (extra) Add(plan, kBlock56);
            break;
        case 1:
            Add(plan, kBody);
            Add(plan, kBlock32);
            if (extra) {
                Add(plan, kBlock48);
                Add(plan, kBlock56);
            }
            break;
        case 2:
            Add(plan, kBody);
            Add(plan, kBlock48);
            if (extra) Add(plan, kBlock56);
            break;
        case 3:
            Add(plan, kBody);
            if (extra) {
                Add(plan, kBlock48);
                Add(plan, kBlock56);
            }
            break;
        default: break;
        }
    }
}

} // namespace

Plan MakePlan(int modifierId, int variant, float scale, float blend, bool extra)
{
    Plan plan;
    if (modifierId == 0) return plan;
    plan.valid = true;
    // scale - 1.0 is compared at full precision and stored as a float; a NaN counts as "not above".
    const double over = static_cast<double>(scale) - 1.0;
    const bool next = over > kEpsilon;
    float weightBase = next ? static_cast<float>(over) : scale;
    const bool blended = static_cast<double>(blend) > kEpsilon;
    if (blended) weightBase = blend;
    plan.weight = static_cast<float>(1.0 - static_cast<double>(weightBase));
    // Four consecutive tables per id: (0,1), (1,2) and with a blend (x,3). Ids 3 and 4 share the last four.
    const int firstTable = modifierId == 1 ? 0 : modifierId == 2 ? 4 : 8;
    plan.lowTable = firstTable + (next ? 1 : 0);
    plan.withTable = blended ? firstTable + 3 : plan.lowTable + 1;
    if (modifierId >= 1 && modifierId <= 4) PickRanges(&plan, modifierId, variant, extra);
    return plan;
}

} // namespace obl::palette_modifiers
