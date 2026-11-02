#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/guest_desire_actions.h"
#include "game/objects/guest_thoughts.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::guest_desire_actions;
using namespace obl::layout;
using obl::object::Guest;
using namespace obl::tables;

namespace {

// k_DesireNames, the order of Guest::m_desires.
const char* const kNames[kDesireCount] = {"ACCOMDTN", "ALCOHOL", "AMUSEMNT", "FIRSTAID", "PILLS", "MEALS", "PARTYFUN", "SHOWER", "SNACKS",
        "SOFTDRNK", "SOUVENIR", "SPORTFUN", "SUNBATHE", "SWIMFUN", "RELIEF", "WATERFUN", "STROLL", "LEAVE", "VANDAL", "FIGHT", "FLIRT",
        "UMBRELS", "LOTION", "HARDSEX", "SLEEP", "SCARED"};

struct Fixture {
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();
    std::unique_ptr<obl::tables::GuestDef> def = std::make_unique<obl::tables::GuestDef>();

    Fixture()
    {
        obl::unit::FillObject<Guest>(*guest);
        *def = GuestDef{};
        guest->m_definition = def.get();
        for (float& desire : guest->m_desires) desire = 0.5f;
        for (float& opinion : guest->m_opinion) opinion = 0.7f;
        def->m_OpinionEffect[Variety][1] = 0.25f;
        std::strcpy(guest->m_WalkAnimation, "D_WALK_DESPERATE");
        std::strcpy(guest->m_StandAnimation, "STAND_X");
        guest->m_activity = 3;
        guest->m_NumTimesBlocked = 4;
    }
};

} // namespace

OBL_TEST_CASE("guest_desire_actions.the_name_table_has_one_slot_per_desire_and_the_first_match_wins")
{
    OBL_CHECK_EQ(FindDesire("ACCOMDTN", kNames), 0);
    OBL_CHECK_EQ(FindDesire("SCARED", kNames), 25);
    OBL_CHECK_EQ(FindDesire("SNACKS", kNames), kSnacks);
    OBL_CHECK_EQ(FindDesire("RELIEF", kNames), kToilet);
    OBL_CHECK_EQ(FindDesire("STROLL", kNames), kStroll);
    OBL_CHECK_EQ(FindDesire("NONE", kNames), kDesireCount);
    OBL_CHECK_EQ(FindDesire("", kNames), kDesireCount);
    OBL_CHECK_EQ(FindDesire("ALCOHOL ", kNames), kDesireCount); // the whole string is compared
    OBL_CHECK_EQ(FindDesire("alcohol", kNames), FindDesire("ALCOHOL", kNames)); // case does not matter
    const char* const doubled[kDesireCount] = {"A", "B", "B"};
    // A repeated name is found from the start and again after it.
    const char* names[kDesireCount];
    for (int i = 0; i < kDesireCount; ++i) names[i] = i < 3 ? doubled[i] : "Z";
    OBL_CHECK_EQ(FindDesire("B", names), 1);
    OBL_CHECK_EQ(FindDesire("B", names, 2), 2);
}

OBL_TEST_CASE("guest_desire_actions.the_first_miss_of_a_round_only_sets_the_flag")
{
    Fixture f;
    f.guest->m_missedDesireLast = 0;
    MissedDesire(*f.guest, *f.def, "MEALS", kNames);
    OBL_CHECK_EQ(f.guest->m_missedDesire, 1);
    OBL_CHECK_EQ(f.guest->m_desires[kMeals], 0.5f);
    OBL_CHECK_EQ(f.guest->m_opinion[Variety], 0.7f);
    OBL_CHECK_EQ(f.guest->m_activity, 3);
}

OBL_TEST_CASE("guest_desire_actions.a_repeated_miss_clears_the_desire_and_costs_variety")
{
    Fixture f;
    f.guest->m_missedDesireLast = 1;
    MissedDesire(*f.guest, *f.def, "MEALS", kNames);
    OBL_CHECK_EQ(f.guest->m_missedDesire, 0); // the flag itself is left to SearchDesire
    OBL_CHECK_EQ(f.guest->m_desires[kMeals], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[Variety], 0.45f);
    OBL_CHECK_EQ(f.guest->m_desires[kSnacks], 0.5f);
    OBL_CHECK_EQ(f.guest->m_activity, 3);
}

OBL_TEST_CASE("guest_desire_actions.a_miss_of_an_outdoor_desire_resets_the_activity_and_spares_variety")
{
    for (int desire : {kSunbathing, kSwimming, kStroll}) {
        Fixture f;
        f.guest->m_missedDesireLast = 1;
        MissedDesire(*f.guest, *f.def, kNames[desire], kNames);
        OBL_CHECK_EQ(f.guest->m_desires[desire], 0.0f);
        OBL_CHECK_EQ(f.guest->m_activity, 0);
        OBL_CHECK_EQ(f.guest->m_opinion[Variety], 0.7f);
    }
}

OBL_TEST_CASE("guest_desire_actions.a_miss_of_the_toilet_spares_variety_and_the_activity")
{
    Fixture f;
    f.guest->m_missedDesireLast = 1;
    MissedDesire(*f.guest, *f.def, "RELIEF", kNames);
    OBL_CHECK_EQ(f.guest->m_desires[kToilet], 0.0f);
    OBL_CHECK_EQ(f.guest->m_activity, 3);
    OBL_CHECK_EQ(f.guest->m_opinion[Variety], 0.7f);
}

OBL_TEST_CASE("guest_desire_actions.variety_is_held_at_zero_by_the_unrounded_difference_and_a_nan_goes_to_zero")
{
    Fixture f;
    f.guest->m_missedDesireLast = 1;
    f.def->m_OpinionEffect[Variety][1] = 0.9f;
    MissedDesire(*f.guest, *f.def, "ALCOHOL", kNames);
    OBL_CHECK_EQ(f.guest->m_opinion[Variety], 0.0f);
    Fixture g;
    g.guest->m_missedDesireLast = 1;
    g.guest->m_opinion[Variety] = std::numeric_limits<float>::quiet_NaN();
    MissedDesire(*g.guest, *g.def, "ALCOHOL", kNames);
    OBL_CHECK_EQ(g.guest->m_opinion[Variety], 0.0f); // unordered counts as below
}

OBL_TEST_CASE("guest_desire_actions.an_unrecognized_family_changes_nothing")
{
    Fixture f;
    f.guest->m_missedDesireLast = 1;
    MissedDesire(*f.guest, *f.def, "NONE", kNames);
    AlterDesires(*f.guest, "NONE", kNames);
    for (float desire : f.guest->m_desires) OBL_CHECK_EQ(desire, 0.5f);
    OBL_CHECK_EQ(f.guest->m_missedDesire, 0);
    OBL_CHECK_EQ(f.guest->m_NumTimesBlocked, 0); // the counter is cleared on every path
}

OBL_TEST_CASE("guest_desire_actions.a_drink_or_a_meal_relieves_the_bladder_desire_up_to_one")
{
    for (int desire : {kAlcohol, kMeals, kSnacks, kSoftDrinks}) {
        Fixture f;
        f.guest->m_desires[kToilet] = 0.3f;
        AlterDesires(*f.guest, kNames[desire], kNames);
        OBL_CHECK_EQ(f.guest->m_desires[desire], 0.0f);
        OBL_CHECK_EQ(f.guest->m_desires[kToilet], static_cast<float>(0.3f + static_cast<double>(0.1f)));
        OBL_CHECK_EQ(f.guest->m_NumTimesBlocked, 0);
        OBL_CHECK_EQ(std::string(f.guest->m_WalkAnimation), std::string("D_WALK_DESPERATE"));
    }
    Fixture high;
    high.guest->m_desires[kToilet] = 0.95f;
    AlterDesires(*high.guest, "SNACKS", kNames);
    OBL_CHECK_EQ(high.guest->m_desires[kToilet], 1.0f);
    Fixture exact;
    exact.guest->m_desires[kToilet] = 0.9f; // 0.9f + 0.1f is not above one in double
    AlterDesires(*exact.guest, "SNACKS", kNames);
    OBL_CHECK(exact.guest->m_desires[kToilet] <= 1.0f);
}

OBL_TEST_CASE("guest_desire_actions.the_bladder_is_not_relieved_in_the_toilet_state")
{
    Fixture f;
    f.guest->m_desires[kToilet] = 0.3f;
    f.guest->m_state = kStateWc;
    AlterDesires(*f.guest, "MEALS", kNames);
    OBL_CHECK_EQ(f.guest->m_desires[kMeals], 0.0f);
    OBL_CHECK_EQ(f.guest->m_desires[kToilet], 0.3f);
}

OBL_TEST_CASE("guest_desire_actions.satisfying_the_toilet_puts_the_animations_back_to_normal")
{
    Fixture f;
    std::memset(f.guest->m_WalkAnimation, 'q', sizeof(f.guest->m_WalkAnimation));
    std::memset(f.guest->m_StandAnimation, 'q', sizeof(f.guest->m_StandAnimation));
    AlterDesires(*f.guest, "RELIEF", kNames);
    OBL_CHECK_EQ(f.guest->m_desires[kToilet], 0.0f);
    OBL_CHECK_EQ(std::string(f.guest->m_WalkAnimation), std::string("D_WALK_NORMAL"));
    OBL_CHECK_EQ(std::string(f.guest->m_StandAnimation), std::string("D_STAND_NORMAL"));
    // strcpy: the bytes after the terminator stay as they were.
    OBL_CHECK_EQ(f.guest->m_WalkAnimation[14], 'q');
    OBL_CHECK_EQ(f.guest->m_StandAnimation[15], 'q');
    OBL_CHECK_EQ(f.guest->m_activity, 3);
}

OBL_TEST_CASE("guest_desire_actions.satisfying_an_outdoor_desire_resets_the_activity")
{
    for (int desire : {kSunbathing, kSwimming, kStroll}) {
        Fixture f;
        AlterDesires(*f.guest, kNames[desire], kNames);
        OBL_CHECK_EQ(f.guest->m_desires[desire], 0.0f);
        OBL_CHECK_EQ(f.guest->m_activity, 0);
    }
    Fixture other;
    AlterDesires(*other.guest, "AMUSEMNT", kNames);
    OBL_CHECK_EQ(other.guest->m_activity, 3);
    OBL_CHECK_EQ(other.guest->m_desires[2], 0.0f);
}

OBL_TEST_CASE("guest_desire_actions.setup_copies_the_start_values_of_the_definition")
{
    Fixture f;
    f.def->m_StartBladder = 0.11f, f.def->m_StartFight = 0.22f, f.def->m_StartFlirt = 0.33f, f.def->m_StartDrunk = 0.44f;
    f.def->m_StartIll = 0.55f, f.def->m_StartSunburn = 0.66f, f.def->m_StartVandalise = 0.77f;
    SetupStartValues(*f.guest, *f.def);
    OBL_CHECK_EQ(f.guest->m_desires[kToilet], 0.11f);
    OBL_CHECK_EQ(f.guest->m_desires[kFight], 0.22f);
    OBL_CHECK_EQ(f.guest->m_desires[kFlirt], 0.33f);
    OBL_CHECK_EQ(f.guest->m_drunk, 0.44f);
    OBL_CHECK_EQ(f.guest->m_foodIll, 0.55f);
    OBL_CHECK_EQ(f.guest->m_sunBurnLevel, 0.66f);
    OBL_CHECK_EQ(f.guest->m_desires[kVandalise], 0.77f);
    OBL_CHECK_EQ(f.guest->m_desires[0], 0.5f); // nothing else
}

OBL_TEST_CASE("guest_thoughts.demolished_asleep_spoils_the_visit_and_lowers_the_opinions")
{
    Fixture f;
    f.guest->m_curScore = 2.0f;
    f.guest->m_opinion[0] = 1.0f, f.guest->m_opinion[1] = 0.15f, f.guest->m_opinion[2] = 0.2f, f.guest->m_opinion[3] = 0.0f;
    f.guest->m_opinion[4] = std::numeric_limits<float>::quiet_NaN(), f.guest->m_opinion[5] = -0.5f, f.guest->m_opinion[6] = 0.5f;
    guest_thoughts::PenaliseDemolishedScore(*f.guest);
    guest_thoughts::PenaliseDemolishedOpinions(*f.guest);
    OBL_CHECK_EQ(f.guest->m_missedPurchase, 1);
    OBL_CHECK_EQ(f.guest->m_curScore, static_cast<float>(2.0 - static_cast<double>(1.4f)));
    OBL_CHECK_EQ(f.guest->m_opinion[0], static_cast<float>(1.0 - static_cast<double>(0.2f)));
    OBL_CHECK_EQ(f.guest->m_opinion[1], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[2], 0.0f); // 0.2f - double(0.2f) is exactly zero: not below
    OBL_CHECK_EQ(f.guest->m_opinion[3], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[4], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[5], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[6], static_cast<float>(0.5 - static_cast<double>(0.2f)));
    OBL_CHECK_EQ(std::string(guest_thoughts::kDemolishedAsleep), std::string("GUEST_THOUGHT_DEMOLISHED_ASLEEP"));
}

OBL_TEST_CASE("guest_thoughts.blocked_message_key_follows_the_activity")
{
    Fixture f;
    f.guest->m_activity = 0;
    f.guest->m_desired_commod = 2;
    OBL_CHECK_EQ(guest_thoughts::BlockedKey(*f.guest, "BAR", 0), std::string("TICKER_MSG_NO_PATH_BAR1"));
    OBL_CHECK_EQ(guest_thoughts::BlockedKey(*f.guest, "BAR", 5), std::string("TICKER_MSG_NO_PATH_BAR3"));
    f.guest->m_activity = 1;
    OBL_CHECK_EQ(guest_thoughts::BlockedKey(*f.guest, nullptr, 1), std::string("TICKER_MSG_NO_PATH_SEA2"));
    f.guest->m_desired_commod = 3;
    OBL_CHECK_EQ(guest_thoughts::BlockedKey(*f.guest, nullptr, 2), std::string("TICKER_MSG_NO_PATH_BEACH3"));
    f.guest->m_desired_commod = 0;
    OBL_CHECK_EQ(guest_thoughts::BlockedKey(*f.guest, nullptr, 3), std::string("TICKER_MSG_NO_PATH_BEACH1"));
}
