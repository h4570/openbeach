#pragma once

#include "game/objects/object_staff.h"

#include <cstddef>
#include <cstdint>

// GetFeedbackText and its profession overrides select thought text from feedback flags. PushThought keeps the three
// latest thoughts. The keys are text lookup keys of the text table; the pure logic here draws through an
// environment and never touches the game:
//   env.Rand()    - the game's rand()
//   env.Female()  - the gender of the staff definition of the object (of the record found by the object's name); only asked on the
//                   branches that read it, before the draw (the draw count depends on it)
namespace obl::staff {

enum class TextKind { Base, Builder, Cleaner, Lifeguard, Mechanic, Rep, Security };

namespace text_detail {

// How a rule answers: a fixed key; one of three keys chosen by `rand() % 3`; or one of three chosen by `rand() % 3` in the male or the female
// set (the keys are laid out male 0..2, female 3..5).
enum class Shape : uint8_t { Fixed, Pick3, Gender3 };

struct Rule {
    uint32_t mask; // the first rule of the list whose mask meets the flags wins
    Shape shape;
    const char* keys[6];
};

constexpr Rule Fixed(uint32_t mask, const char* key)
{
    return {mask, Shape::Fixed, {key, nullptr, nullptr, nullptr, nullptr, nullptr}};
}
constexpr Rule Pick3(uint32_t mask, const char* a, const char* b, const char* c)
{
    return {mask, Shape::Pick3, {a, b, c, nullptr, nullptr, nullptr}};
}
constexpr Rule Gender3(uint32_t mask, const char* m0, const char* m1, const char* m2, const char* f0, const char* f1, const char* f2)
{
    return {mask, Shape::Gender3, {m0, m1, m2, f0, f1, f2}};
}

// The order of the rules matters (docs/architecture/objects.md); a rule that draws consumes one `rand()` and answers
// with the key it picked. The list of a profession is tried after the base class has answered (and possibly drawn), and replaces the base
// answer whenever one of its rules fires.
inline constexpr Rule kBase[] = {
        Fixed(1u << 22, "FEEDBACK_STAFF_CANT_DO_JOB"),
        Fixed(0x200000, "FEEDBACK_STAFF_NEXT_JOB_I_DO"),
        Fixed(0x800000, "FEEDBACK_STAFFTHOUGHT_UNHAPPY"),
        Fixed(0x1000000, "FEEDBACK_STAFFTHOUGHT_STARTWORK"),
        Fixed(0x2000000, "FEEDBACK_STAFFTHOUGHT_ENDWORK"),
        Fixed(0x4000000, "FEEDBACK_STAFFTHOUGHT_PAYRISE"),
        Fixed(0x8000000, "FEEDBACK_STAFFTHOUGHT_PAYCUT"),
        Fixed(0x10000000, "FEEDBACK_STAFF_SACKED"),
        Pick3(0x40000000, "FEEDBACK_STAFF_DIRECTED_01", "FEEDBACK_STAFF_DIRECTED_02", "FEEDBACK_STAFF_DIRECTED_03"),
        Pick3(0x80000000u, "FEEDBACK_STAFF_OVERWORKED_00", "FEEDBACK_STAFF_OVERWORKED_01", "FEEDBACK_STAFF_OVERWORKED_02"),
};

inline constexpr Rule kBuilder[] = {
        Fixed(0x2, "FEEDBACK_BUILDER_WOKEN_UP"),
        Fixed(0x100000, "FEEDBACK_BUILDER_UNHAPPY"),
        Fixed(0x4, "FEEDBACK_BUILDER_FULL_QUOTA"),
        Pick3(0x20000000, "FEEDBACK_BUILDER_INNERTOUGHT_STANDIDLE_01", "FEEDBACK_BUILDER_INNERTOUGHT_STANDIDLE_02",
                "FEEDBACK_BUILDER_INNERTOUGHT_STANDIDLE_03"),
        Pick3(0x8, "FEEDBACK_BUILDER_INNERTOUGHT_CONSTRUCT_01", "FEEDBACK_BUILDER_INNERTOUGHT_CONSTRUCT_02",
                "FEEDBACK_BUILDER_INNERTOUGHT_CONSTRUCT_03"),
        Pick3(0x10, "FEEDBACK_BUILDER_INNERTOUGHT_DEMOLISH_01", "FEEDBACK_BUILDER_INNERTOUGHT_DEMOLISH_02",
                "FEEDBACK_BUILDER_INNERTOUGHT_DEMOLISH_03"),
        Pick3(0x20, "FEEDBACK_BUILDER_INNERTOUGHT_WHISTLING_01", "FEEDBACK_BUILDER_INNERTOUGHT_WHISTLING_02",
                "FEEDBACK_BUILDER_INNERTOUGHT_WHISTLING_03"),
};

inline constexpr Rule kCleaner[] = {
        Fixed(0x4, "FEEDBACK_CLEANER_NO_TOILETS"),
        Fixed(0x2, "FEEDBACK_CLEANER_TOILETS_BUSY"),
        Fixed(0x100000, "FEEDBACK_CLEANER_UNHAPPY"),
        Pick3(0x8, "FEEDBACK_CLEANER_INNERTOUGHT_EMPTYBIN_01", "FEEDBACK_CLEANER_INNERTOUGHT_EMPTYBIN_02",
                "FEEDBACK_CLEANER_INNERTOUGHT_EMPTYBIN_03"),
        Pick3(0x10, "FEEDBACK_CLEANER_INNERTOUGHT_CLEANTOILET_01", "FEEDBACK_CLEANER_INNERTOUGHT_CLEANTOILET_02",
                "FEEDBACK_CLEANER_INNERTOUGHT_CLEANTOILET_03"),
        Pick3(0x40, "FEEDBACK_CLEANER_INNERTOUGHT_CLEANLITTER_01", "FEEDBACK_CLEANER_INNERTOUGHT_CLEANLITTER_02",
                "FEEDBACK_CLEANER_INNERTOUGHT_CLEANLITTER_03"),
        Pick3(0x20, "FEEDBACK_CLEANER_INNERTOUGHT_GOSTROLL_01", "FEEDBACK_CLEANER_INNERTOUGHT_GOSTROLL_02",
                "FEEDBACK_CLEANER_INNERTOUGHT_GOSTROLL_03"),
        Pick3(0x20000000, "FEEDBACK_CLEANER_INNERTOUGHT_STANDIDLE_01", "FEEDBACK_CLEANER_INNERTOUGHT_STANDIDLE_02",
                "FEEDBACK_CLEANER_INNERTOUGHT_STANDIDLE_03"),
};

inline constexpr Rule kLifeguard[] = {
        Fixed(0x2, "FEEDBACK_LIFEGUARD_GUESTS_DROWNED"),
        Fixed(0x4, "FEEDBACK_LIFEGUARD_SWIM_IN_TROUBLE"),
        Fixed(0x8, "FEEDBACK_LIFEGUARD_CANT_SAVE"),
        Fixed(0x10, "FEEDBACK_LIFEGUARD_ALREADY_SAVED"),
        Fixed(0x20, "FEEDBACK_LIFEGUARD_IS_OK"),
        Fixed(0x100000, "FEEDBACK_LIFEGUARD_UNHAPPY"),
        Fixed(0x40, "FEEDBACK_LIFEGUARD_GUESTINTROUBLE"),
        Fixed(0x80, "FEEDBACK_LIFEGUARD_GUESTRESCUED"),
        Gender3(0x20000000, "FEEDBACK_LIFEGUARD_INNERTOUGHT_MALE_STANDIDLE_01", "FEEDBACK_LIFEGUARD_INNERTOUGHT_MALE_STANDIDLE_02",
                "FEEDBACK_LIFEGUARD_INNERTOUGHT_MALE_STANDIDLE_03", "FEEDBACK_LIFEGUARD_INNERTOUGHT_FEMALE_STANDIDLE_01",
                "FEEDBACK_LIFEGUARD_INNERTOUGHT_FEMALE_STANDIDLE_02", "FEEDBACK_LIFEGUARD_INNERTOUGHT_FEMALE_STANDIDLE_03"),
};

inline constexpr Rule kMechanic[] = {
        Fixed(0x2, "FEEDBACK_MECHANIC_TOO_BUSY"),
        Fixed(0x100000, "FEEDBACK_MECHANIC_UNHAPPY"),
        Fixed(0x4, "FEEDBACK_MECHANIC_INNERTOUGHT_INSPECTIONMADE"),
        Fixed(0x8, "FEEDBACK_MECHANIC_BUILDING_INNERTOUGHT_REPAIRMADE"),
        Fixed(0x10, "FEEDBACK_MECHANIC_SCENERY_INNERTOUGHT_REPAIRMADE"),
        Fixed(0x20, "FEEDBACK_MECHANIC_SCENERY_INNERTOUGHT_WATERED"),
        Pick3(0x20000000, "FEEDBACK_MECHANIC_INNERTOUGHT_STANDIDLE_01", "FEEDBACK_MECHANIC_INNERTOUGHT_STANDIDLE_02",
                "FEEDBACK_MECHANIC_INNERTOUGHT_STANDIDLE_03"),
};

inline constexpr Rule kRep[] = {
        Fixed(0x2, "FEEDBACK_REP_BEAUTY"),
        Fixed(0x4, "FEEDBACK_REP_CROWDED"),
        Fixed(0x8, "FEEDBACK_REP_NO_TOTTY"),
        Fixed(0x10, "FEEDBACK_REP_EXPENSIVE"),
        Fixed(0x20, "FEEDBACK_REP_BORING"),
        Fixed(0x40, "FEEDBACK_REP_DIRTY"),
        Fixed(0x80, "FEEDBACK_REP_DANGER"),
        Fixed(0x100, "FEEDBACK_REP_PEACEFUL"),
        Fixed(0x200, "FEEDBACK_REP_NOT_CROWDED"),
        Fixed(0x400, "FEEDBACK_REP_GOOD_TOTTY"),
        Fixed(0x800, "FEEDBACK_REP_PRICE_RIGHT"),
        Fixed(0x1000, "FEEDBACK_REP_LOTS_TO_DO"),
        Fixed(0x2000, "FEEDBACK_REP_CLEAN"),
        Fixed(0x4000, "FEEDBACK_REP_SAFE"),
        Fixed(0x8000, "FEEDBACK_REP_LOW_SALES"),
        Fixed(0x10000, "FEEDBACK_REP_HIGH_SALES"),
        Fixed(0x100000, "FEEDBACK_REP_UNHAPPY"),
        Gender3(0x20000000, "FEEDBACK_REP_INNERTOUGHT_MALE_STANDIDLE_01", "FEEDBACK_REP_INNERTOUGHT_MALE_STANDIDLE_02",
                "FEEDBACK_REP_INNERTOUGHT_MALE_STANDIDLE_03", "FEEDBACK_REP_INNERTOUGHT_FEMALE_STANDIDLE_01",
                "FEEDBACK_REP_INNERTOUGHT_FEMALE_STANDIDLE_02", "FEEDBACK_REP_INNERTOUGHT_FEMALE_STANDIDLE_03"),
        Gender3(0x20000, "FEEDBACK_REP_INNERTOUGHT_MALE_PATROLLING_01", "FEEDBACK_REP_INNERTOUGHT_MALE_PATROLLING_02",
                "FEEDBACK_REP_INNERTOUGHT_MALE_PATROLLING_03", "FEEDBACK_REP_INNERTOUGHT_FEMALE_PATROLLING_01",
                "FEEDBACK_REP_INNERTOUGHT_FEMALE_PATROLLING_02", "FEEDBACK_REP_INNERTOUGHT_FEMALE_PATROLLING_03"),
};

inline constexpr Rule kSecurity[] = {
        Fixed(0x2, "FEEDBACK_SECURITYGUARD_NO_BINS"),
        Fixed(0x4, "FEEDBACK_SECURITYGUARD_VANDALS"),
        Fixed(0x8, "FEEDBACK_SECURITYGUARD_FIGHTS"),
        Fixed(0x10, "FEEDBACK_SECURITYGUARD_JAILFULL"),
        Fixed(0x100000, "FEEDBACK_SECURITY_UNHAPPY"),
        Fixed(0x20, "FEEDBACK_SECURITY_SUSPICIOUS"),
        Fixed(0x40, "FEEDBACK_SECURITY_FINE_LITTER"),
        Fixed(0x80, "FEEDBACK_SECURITY_FINE_VANDALISM"),
        Fixed(0x100, "FEEDBACK_SECURITY_FINE_FIGHTING"),
        Fixed(0x200, "FEEDBACK_SECURITY_DETAIN_LITTER"),
        Fixed(0x400, "FEEDBACK_SECURITY_DETAIN_VANDAL"),
        Fixed(0x800, "FEEDBACK_SECURITY_DETAIN_FIGHTER"),
        Pick3(0x20000000, "FEEDBACK_SECURITY_INNERTOUGHT_STANDIDLE_01", "FEEDBACK_SECURITY_INNERTOUGHT_STANDIDLE_02",
                "FEEDBACK_SECURITY_INNERTOUGHT_STANDIDLE_03"),
};

// Walks the rules; null when none fires (or the draw left 0..2, which `rand() % 3` cannot do but the rules still fall through on).
template<class Env, size_t N> const char* Answer(const Rule (&rules)[N], uint32_t flags, Env& env)
{
    for (const Rule& rule : rules) {
        if ((flags & rule.mask) == 0) continue;
        switch (rule.shape) {
        case Shape::Fixed: return rule.keys[0];
        case Shape::Pick3: {
            const int pick = env.Rand() % 3;
            return pick >= 0 && pick < 3 ? rule.keys[pick] : nullptr;
        }
        case Shape::Gender3: {
            const bool female = env.Female();
            const int pick = env.Rand() % 3;
            return pick >= 0 && pick < 3 ? rule.keys[pick + (female ? 3 : 0)] : nullptr;
        }
        }
    }
    return nullptr;
}

} // namespace text_detail

// GetFeedbackText and its six overrides: the text lookup key (null = nothing to say). An override first runs the base
// class, so the base's draw (a `DIRECTED` or `OVERWORKED` flag) happens even when the profession then answers with a fixed key; the answer of
// the profession wins when one of its rules fires, otherwise the base answer stays.
template<class Env> const char* FeedbackText(TextKind kind, uint32_t flags, Env& env)
{
    const char* text = text_detail::Answer(text_detail::kBase, flags, env);
    const char* own = nullptr;
    switch (kind) {
    case TextKind::Base: return text;
    case TextKind::Builder: own = text_detail::Answer(text_detail::kBuilder, flags, env); break;
    case TextKind::Cleaner: own = text_detail::Answer(text_detail::kCleaner, flags, env); break;
    case TextKind::Lifeguard: own = text_detail::Answer(text_detail::kLifeguard, flags, env); break;
    case TextKind::Mechanic: own = text_detail::Answer(text_detail::kMechanic, flags, env); break;
    case TextKind::Rep: own = text_detail::Answer(text_detail::kRep, flags, env); break;
    case TextKind::Security: own = text_detail::Answer(text_detail::kSecurity, flags, env); break;
    }
    return own ? own : text;
}

// PushThought: puts the text of a thought at the head of `m_Thoughts` (three buffers of 256 wide chars, newest first) and
// counts it in `m_NumFeedBackMesslog`. Additional env:
//   env.FeedbackKey(flags)  - the virtual GetFeedbackText of the object
//   env.Lookup(key)         - text lookup on the game's text table
// The duplicate guard compares the *address* of the newest buffer with the pointer returned by the text lookup (a pointer into the text table), which
// never matches: the virtual method is asked twice, so a draw is made for it as well (the draw count is pinned). A counter above 3 (not
// reachable from a saved game of this build) would write past the array, so the buffers are addressed by bytes.
template<class Env> void PushThought(object::Staff& staff, uint32_t flags, Env& env)
{
    auto buffer = [&](int32_t index) {
        return reinterpret_cast<Char16*>(reinterpret_cast<uint8_t*>(staff.m_Thoughts) + static_cast<int64_t>(index) * 0x200);
    };
    auto copy = [](Char16* to, const Char16* from) {
        while ((*to++ = *from++) != 0) {}
    };
    // DEC + JS: the sign of the wrapped result, so the most negative counter counts as non-negative.
    if (static_cast<int32_t>(static_cast<uint32_t>(staff.m_NumFeedBackMesslog) - 1u) >= 0) {
        const Char16* text = env.Lookup(env.FeedbackKey(flags));
        if (buffer(staff.m_NumFeedBackMesslog - 1) == text) return;
    }
    if (staff.m_NumFeedBackMesslog == 0) {
        copy(buffer(staff.m_NumFeedBackMesslog), env.Lookup(env.FeedbackKey(flags)));
        staff.m_NumFeedBackMesslog = staff.m_NumFeedBackMesslog + 1;
        return;
    }
    if (staff.m_NumFeedBackMesslog != 3) staff.m_NumFeedBackMesslog = staff.m_NumFeedBackMesslog + 1;
    for (int32_t i = staff.m_NumFeedBackMesslog - 1; i != 0; --i) copy(buffer(i), buffer(i - 1));
    copy(buffer(0), env.Lookup(env.FeedbackKey(flags)));
}

} // namespace obl::staff
