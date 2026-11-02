#include "object_storage.h"
#include "core/char16.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"

#include "game/objects/staff_thoughts.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::staff::TextKind;

namespace {

// The game side as a script: draws come from a list (the last value repeats), the gender is a flag, and every question is counted.
struct TextEnv {
    std::vector<int> draws{0};
    bool female = false;
    int randCalls = 0;
    int femaleCalls = 0;
    int Rand()
    {
        const int value = draws[std::min<size_t>(static_cast<size_t>(randCalls), draws.size() - 1)];
        ++randCalls;
        return value;
    }
    bool Female()
    {
        ++femaleCalls;
        return female;
    }
};

std::string Key(TextKind kind, uint32_t flags, TextEnv& env)
{
    const char* key = staff::FeedbackText(kind, flags, env);
    return key ? key : "(null)";
}

std::string Key(TextKind kind, uint32_t flags, std::vector<int> draws = {0}, bool female = false)
{
    TextEnv env;
    env.draws = std::move(draws);
    env.female = female;
    return Key(kind, flags, env);
}

} // namespace

OBL_TEST_CASE("staff_thoughts.base_fixed_keys_in_the_fixed_order")
{
    OBL_CHECK_EQ(Key(TextKind::Base, 0x400000), std::string("FEEDBACK_STAFF_CANT_DO_JOB"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0x200000), std::string("FEEDBACK_STAFF_NEXT_JOB_I_DO"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0x800000), std::string("FEEDBACK_STAFFTHOUGHT_UNHAPPY"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0x1000000), std::string("FEEDBACK_STAFFTHOUGHT_STARTWORK"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0x2000000), std::string("FEEDBACK_STAFFTHOUGHT_ENDWORK"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0x4000000), std::string("FEEDBACK_STAFFTHOUGHT_PAYRISE"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0x8000000), std::string("FEEDBACK_STAFFTHOUGHT_PAYCUT"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0x10000000), std::string("FEEDBACK_STAFF_SACKED"));
    // The lowest bit that is set wins (CANT_DO_JOB before everything else, including the draws).
    TextEnv env;
    OBL_CHECK_EQ(Key(TextKind::Base, 0x400000 | 0x40000000 | 0x80000000u, env), std::string("FEEDBACK_STAFF_CANT_DO_JOB"));
    OBL_CHECK_EQ(env.randCalls, 0);
    OBL_CHECK_EQ(Key(TextKind::Base, 0x10000000 | 0x8000000), std::string("FEEDBACK_STAFFTHOUGHT_PAYCUT"));
    OBL_CHECK_EQ(Key(TextKind::Base, 0), std::string("(null)"));
    OBL_CHECK_EQ(
            Key(TextKind::Base, 0x1 | 0x2 | 0x100000 | 0x20000000), std::string("(null)")); // the profession bits mean nothing to the base
}

OBL_TEST_CASE("staff_thoughts.base_draws_directed_and_overworked_variants")
{
    for (int pick = 0; pick < 3; ++pick) {
        TextEnv env;
        env.draws = {pick + 3 * 100}; // only the remainder counts
        OBL_CHECK_EQ(Key(TextKind::Base, 0x40000000, env), "FEEDBACK_STAFF_DIRECTED_0" + std::to_string(pick + 1));
        OBL_CHECK_EQ(env.randCalls, 1);
        TextEnv other;
        other.draws = {pick + 3 * 7};
        OBL_CHECK_EQ(Key(TextKind::Base, 0x80000000u, other), "FEEDBACK_STAFF_OVERWORKED_0" + std::to_string(pick));
        OBL_CHECK_EQ(other.randCalls, 1);
    }
    TextEnv both;
    both.draws = {1, 2};
    OBL_CHECK_EQ(Key(TextKind::Base, 0x40000000 | 0x80000000u, both),
            std::string("FEEDBACK_STAFF_DIRECTED_02")); // directed first, one draw only
    OBL_CHECK_EQ(both.randCalls, 1);
}

OBL_TEST_CASE("staff_thoughts.profession_asks_the_base_first_and_its_draw_is_spent_even_when_it_answers_fixed")
{
    TextEnv env;
    env.draws = {2, 1};
    // The cleaner says NO_TOILETS, but the base class already drew for the DIRECTED bit.
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x4 | 0x40000000, env), std::string("FEEDBACK_CLEANER_NO_TOILETS"));
    OBL_CHECK_EQ(env.randCalls, 1);
    // A flag the profession has no rule for leaves the base answer (and its single draw).
    TextEnv base;
    base.draws = {2};
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x40000000, base), std::string("FEEDBACK_STAFF_DIRECTED_03"));
    OBL_CHECK_EQ(base.randCalls, 1);
    // Both draw: the base one first, then the profession's own.
    TextEnv both;
    both.draws = {1, 2};
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x40000000 | 0x20000000, both), std::string("FEEDBACK_MECHANIC_INNERTOUGHT_STANDIDLE_03"));
    OBL_CHECK_EQ(both.randCalls, 2);
    // A profession rule replaces a base fixed key.
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x2 | 0x10000000), std::string("FEEDBACK_BUILDER_WOKEN_UP"));
    OBL_CHECK_EQ(
            Key(TextKind::Builder, 0x1000000), std::string("FEEDBACK_STAFFTHOUGHT_STARTWORK")); // nothing of the builder's: the base stays
}

OBL_TEST_CASE("staff_thoughts.cleaner_order_and_variants")
{
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x4 | 0x2), std::string("FEEDBACK_CLEANER_NO_TOILETS"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x2 | 0x100000), std::string("FEEDBACK_CLEANER_TOILETS_BUSY"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x100000 | 0x8), std::string("FEEDBACK_CLEANER_UNHAPPY"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x8 | 0x10, {1}), std::string("FEEDBACK_CLEANER_INNERTOUGHT_EMPTYBIN_02"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x10 | 0x40, {2}), std::string("FEEDBACK_CLEANER_INNERTOUGHT_CLEANTOILET_03"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x40 | 0x20, {0}), std::string("FEEDBACK_CLEANER_INNERTOUGHT_CLEANLITTER_01"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x20 | 0x20000000, {1}), std::string("FEEDBACK_CLEANER_INNERTOUGHT_GOSTROLL_02"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x20000000, {2}), std::string("FEEDBACK_CLEANER_INNERTOUGHT_STANDIDLE_03"));
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x1), std::string("(null)"));
}

OBL_TEST_CASE("staff_thoughts.builder_order_and_variants")
{
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x2 | 0x100000), std::string("FEEDBACK_BUILDER_WOKEN_UP"));
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x100000 | 0x4), std::string("FEEDBACK_BUILDER_UNHAPPY"));
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x4 | 0x20000000), std::string("FEEDBACK_BUILDER_FULL_QUOTA"));
    // The idle thought comes before the three work thoughts.
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x20000000 | 0x8, {0}), std::string("FEEDBACK_BUILDER_INNERTOUGHT_STANDIDLE_01"));
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x8 | 0x10, {1}), std::string("FEEDBACK_BUILDER_INNERTOUGHT_CONSTRUCT_02"));
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x10 | 0x20, {2}), std::string("FEEDBACK_BUILDER_INNERTOUGHT_DEMOLISH_03"));
    OBL_CHECK_EQ(Key(TextKind::Builder, 0x20, {1}), std::string("FEEDBACK_BUILDER_INNERTOUGHT_WHISTLING_02"));
}

OBL_TEST_CASE("staff_thoughts.mechanic_and_security_order")
{
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x2 | 0x100000), std::string("FEEDBACK_MECHANIC_TOO_BUSY"));
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x100000 | 0x4), std::string("FEEDBACK_MECHANIC_UNHAPPY"));
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x4 | 0x8), std::string("FEEDBACK_MECHANIC_INNERTOUGHT_INSPECTIONMADE"));
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x8 | 0x10), std::string("FEEDBACK_MECHANIC_BUILDING_INNERTOUGHT_REPAIRMADE"));
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x10 | 0x20), std::string("FEEDBACK_MECHANIC_SCENERY_INNERTOUGHT_REPAIRMADE"));
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x20 | 0x20000000), std::string("FEEDBACK_MECHANIC_SCENERY_INNERTOUGHT_WATERED"));
    OBL_CHECK_EQ(Key(TextKind::Security, 0x2), std::string("FEEDBACK_SECURITYGUARD_NO_BINS"));
    OBL_CHECK_EQ(Key(TextKind::Security, 0x10 | 0x100000), std::string("FEEDBACK_SECURITYGUARD_JAILFULL"));
    OBL_CHECK_EQ(Key(TextKind::Security, 0x100000 | 0x20), std::string("FEEDBACK_SECURITY_UNHAPPY"));
    OBL_CHECK_EQ(Key(TextKind::Security, 0x20 | 0x40), std::string("FEEDBACK_SECURITY_SUSPICIOUS"));
    OBL_CHECK_EQ(Key(TextKind::Security, 0x800), std::string("FEEDBACK_SECURITY_DETAIN_FIGHTER"));
    OBL_CHECK_EQ(Key(TextKind::Security, 0x800 | 0x20000000, {0}), std::string("FEEDBACK_SECURITY_DETAIN_FIGHTER"));
    OBL_CHECK_EQ(Key(TextKind::Security, 0x20000000, {1}), std::string("FEEDBACK_SECURITY_INNERTOUGHT_STANDIDLE_02"));
}

OBL_TEST_CASE("staff_thoughts.rep_order_and_gendered_variants")
{
    OBL_CHECK_EQ(Key(TextKind::Rep, 0x2 | 0x4), std::string("FEEDBACK_REP_BEAUTY"));
    OBL_CHECK_EQ(Key(TextKind::Rep, 0x10000), std::string("FEEDBACK_REP_HIGH_SALES"));
    OBL_CHECK_EQ(Key(TextKind::Rep, 0x100000 | 0x10000), std::string("FEEDBACK_REP_HIGH_SALES")); // the sales bits come before UNHAPPY
    OBL_CHECK_EQ(Key(TextKind::Rep, 0x100000 | 0x20000000), std::string("FEEDBACK_REP_UNHAPPY"));
    // Idle before patrolling; the gender is asked (once) before the draw.
    TextEnv male;
    male.draws = {2};
    OBL_CHECK_EQ(Key(TextKind::Rep, 0x20000000 | 0x20000, male), std::string("FEEDBACK_REP_INNERTOUGHT_MALE_STANDIDLE_03"));
    OBL_CHECK_EQ(male.femaleCalls, 1);
    OBL_CHECK_EQ(male.randCalls, 1);
    TextEnv female;
    female.draws = {1};
    female.female = true;
    OBL_CHECK_EQ(Key(TextKind::Rep, 0x20000, female), std::string("FEEDBACK_REP_INNERTOUGHT_FEMALE_PATROLLING_02"));
    OBL_CHECK_EQ(female.femaleCalls, 1);
    // Other branches never ask for the gender.
    TextEnv none;
    OBL_CHECK_EQ(Key(TextKind::Rep, 0x2, none), std::string("FEEDBACK_REP_BEAUTY"));
    OBL_CHECK_EQ(none.femaleCalls, 0);
}

OBL_TEST_CASE("staff_thoughts.lifeguard_order_and_gendered_variants")
{
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x2 | 0x4), std::string("FEEDBACK_LIFEGUARD_GUESTS_DROWNED"));
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x20 | 0x100000), std::string("FEEDBACK_LIFEGUARD_IS_OK"));
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x100000 | 0x40), std::string("FEEDBACK_LIFEGUARD_UNHAPPY"));
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x40 | 0x80), std::string("FEEDBACK_LIFEGUARD_GUESTINTROUBLE"));
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x80 | 0x20000000), std::string("FEEDBACK_LIFEGUARD_GUESTRESCUED"));
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x20000000, {0}, false), std::string("FEEDBACK_LIFEGUARD_INNERTOUGHT_MALE_STANDIDLE_01"));
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x20000000, {1}, true), std::string("FEEDBACK_LIFEGUARD_INNERTOUGHT_FEMALE_STANDIDLE_02"));
    OBL_CHECK_EQ(Key(TextKind::Lifeguard, 0x20000000, {2}, true), std::string("FEEDBACK_LIFEGUARD_INNERTOUGHT_FEMALE_STANDIDLE_03"));
}

OBL_TEST_CASE("staff_thoughts.a_draw_out_of_range_leaves_the_base_answer")
{
    OBL_CHECK_EQ(Key(TextKind::Cleaner, 0x20000000, {-1}), std::string("(null)"));
    OBL_CHECK_EQ(Key(TextKind::Mechanic, 0x20000000 | 0x40000000, {1, -2}), std::string("FEEDBACK_STAFF_DIRECTED_02"));
}

// ---- PushThought -------------------------------------------------------------------------------------------------------------

namespace {

// The thought buffers hold the game's 2-byte text (Char16).
using Text = std::basic_string<Char16>;

void Copy(Char16* to, const Char16* from)
{
    while ((*to++ = *from++) != 0) {}
}

struct ThoughtEnv {
    std::vector<Text> texts; // what the text lookup routine answers, per call of FeedbackKey
    std::vector<const Char16*> forced; // Lookup answers a given pointer instead (the duplicate guard)
    size_t keys = 0;
    size_t lookups = 0;
    const char* FeedbackKey(uint32_t) { return reinterpret_cast<const char*>(++keys); }
    const Char16* Lookup(const char* key)
    {
        ++lookups;
        const size_t index = reinterpret_cast<size_t>(key) - 1;
        if (index < forced.size() && forced[index]) return forced[index];
        return texts[std::min(index, texts.size() - 1)].c_str();
    }
};

std::unique_ptr<object::Staff> Staff()
{
    auto staff = std::make_unique<object::Staff>();
    obl::unit::FillObject<object::Staff>(*staff);
    return staff;
}

Text Thought(const object::Staff& staff, int index)
{
    return staff.m_Thoughts[index];
}

} // namespace

OBL_TEST_CASE("staff_thoughts.push_into_an_empty_list")
{
    auto staff = Staff();
    ThoughtEnv env;
    env.texts = {u"first"};
    staff::PushThought(*staff, 1, env);
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 1);
    OBL_CHECK(Thought(*staff, 0) == u"first");
    OBL_CHECK_EQ(env.keys, size_t{1}); // nothing to compare with: the text is asked once
}

OBL_TEST_CASE("staff_thoughts.push_shifts_the_older_thoughts_and_keeps_three")
{
    // The first push asks once; every later one asks twice (the duplicate guard, then the real push) and stores the second answer.
    auto staff = Staff();
    ThoughtEnv env;
    env.texts = {u"one", u"two", u"three", u"four", u"five", u"six", u"seven"};
    staff::PushThought(*staff, 0, env);
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 1);
    OBL_CHECK(Thought(*staff, 0) == u"one");
    staff::PushThought(*staff, 0, env);
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 2);
    OBL_CHECK(Thought(*staff, 0) == u"three");
    OBL_CHECK(Thought(*staff, 1) == u"one");
    staff::PushThought(*staff, 0, env);
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 3);
    OBL_CHECK(Thought(*staff, 0) == u"five");
    OBL_CHECK(Thought(*staff, 1) == u"three");
    OBL_CHECK(Thought(*staff, 2) == u"one");
    staff::PushThought(*staff, 0, env);
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 3); // capped
    OBL_CHECK(Thought(*staff, 0) == u"seven");
    OBL_CHECK(Thought(*staff, 1) == u"five");
    OBL_CHECK(Thought(*staff, 2) == u"three");
}

OBL_TEST_CASE("staff_thoughts.push_asks_twice_when_the_list_is_not_empty")
{
    // The duplicate guard asks the virtual GetFeedbackText (a draw may hide behind it) and the real push asks again: two questions per push.
    auto staff = Staff();
    staff->m_NumFeedBackMesslog = 1;
    Copy(staff->m_Thoughts[0], u"old");
    ThoughtEnv env;
    env.texts = {u"guard", u"new"};
    staff::PushThought(*staff, 0, env);
    OBL_CHECK_EQ(env.keys, size_t{2});
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 2);
    OBL_CHECK(Thought(*staff, 0) == u"new"); // the second answer is the one that is stored
    OBL_CHECK(Thought(*staff, 1) == u"old");
}

OBL_TEST_CASE("staff_thoughts.push_at_three_drops_the_oldest")
{
    auto staff = Staff();
    staff->m_NumFeedBackMesslog = 3;
    Copy(staff->m_Thoughts[0], u"newest");
    Copy(staff->m_Thoughts[1], u"middle");
    Copy(staff->m_Thoughts[2], u"oldest");
    ThoughtEnv env;
    env.texts = {u"guard", u"fresh"};
    staff::PushThought(*staff, 0, env);
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 3);
    OBL_CHECK(Thought(*staff, 0) == u"fresh");
    OBL_CHECK(Thought(*staff, 1) == u"newest");
    OBL_CHECK(Thought(*staff, 2) == u"middle");
}

OBL_TEST_CASE("staff_thoughts.push_duplicate_guard_compares_addresses_and_fires_only_on_the_buffer_itself")
{
    auto staff = Staff();
    staff->m_NumFeedBackMesslog = 2;
    Copy(staff->m_Thoughts[0], u"a");
    Copy(staff->m_Thoughts[1], u"b");
    ThoughtEnv same;
    same.texts = {u"b"}; // the same words in another buffer: the guard does not see them
    staff::PushThought(*staff, 0, same);
    OBL_CHECK_EQ(staff->m_NumFeedBackMesslog, 3);

    // Only the address of the newest buffer counts (buffer count - 1), which is what a the text lookup routine answer never is in the game.
    auto other = Staff();
    other->m_NumFeedBackMesslog = 2;
    Copy(other->m_Thoughts[0], u"a");
    Copy(other->m_Thoughts[1], u"b");
    ThoughtEnv guard;
    guard.texts = {u"x"};
    guard.forced = {other->m_Thoughts[1]};
    staff::PushThought(*other, 0, guard);
    OBL_CHECK_EQ(other->m_NumFeedBackMesslog, 2);
    OBL_CHECK(Thought(*other, 0) == u"a");
    OBL_CHECK_EQ(guard.keys, size_t{1});
}
