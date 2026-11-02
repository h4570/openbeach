#include "game/ui/sprite.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/guest_thoughts.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::guest_thoughts;
using namespace obl::layout;
using obl::object::Guest;
using namespace obl::tables;

namespace {

struct Fixture {
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();
    std::unique_ptr<obl::tables::GuestDef> def = std::make_unique<obl::tables::GuestDef>();
    obl::ui::SpriteInstance icon{};

    Fixture()
    {
        obl::unit::FillObject<Guest>(*guest);
        *def = GuestDef{};
        guest->m_definition = def.get();
        guest->m_moodIcon = &icon;
    }
};

struct FakeEnv {
    int rand = 0;
    int32_t id = 41; // answer for every key but the ones listed in `missing`
    std::vector<std::string> missing;
    std::vector<std::string> keys;
    int Rand() { return rand; }
    int32_t TextId(const char* key)
    {
        keys.emplace_back(key);
        for (const std::string& m : missing)
            if (m == key) return -1;
        return id;
    }
};

} // namespace

OBL_TEST_CASE("guest_thoughts.key_is_the_prefix_and_a_digit_from_one_to_three")
{
    Fixture f;
    FakeEnv env;
    for (int draw : {0, 1, 2, 3, 7, 32767}) {
        env.rand = draw;
        ThinkOf(*f.guest, kDetained, env);
    }
    OBL_CHECK_EQ(env.keys.size(), 6u);
    OBL_CHECK_EQ(env.keys[0], std::string("GUEST_THOUGHT_DETAINED1"));
    OBL_CHECK_EQ(env.keys[1], std::string("GUEST_THOUGHT_DETAINED2"));
    OBL_CHECK_EQ(env.keys[2], std::string("GUEST_THOUGHT_DETAINED3"));
    OBL_CHECK_EQ(env.keys[3], std::string("GUEST_THOUGHT_DETAINED1"));
    OBL_CHECK_EQ(env.keys[4], std::string("GUEST_THOUGHT_DETAINED2")); // 7 % 3 + 1
    OBL_CHECK_EQ(env.keys[5], std::string("GUEST_THOUGHT_DETAINED2")); // 32767 % 3 + 1
}

OBL_TEST_CASE("guest_thoughts.prefixes_are_spelled_like_the_exe")
{
    // Some end in a separator before the digit and some do not; the text table has the keys the same way.
    OBL_CHECK_EQ(std::string(kIllPurchase), std::string("GUEST_THOUGHT_NICE_"));
    OBL_CHECK_EQ(std::string(kBeachClosed), std::string("GUEST_THOUGHT_BEACH_CLOSED_"));
    OBL_CHECK_EQ(std::string(kDetainedForNothing), std::string("GUEST_THOUGHT_DETAINED_FOR_NOTHING"));
    OBL_CHECK_EQ(std::string(kNoPathToAccommodation), std::string("GUEST_THOUGHT_NO_PATH_ACCOM"));
    OBL_CHECK_EQ(std::string(kBuildingFull), std::string("GUEST_THOUGHT_FULL_"));
    OBL_CHECK_EQ(std::string(kNoPathTicker), std::string("TICKER_MSG_GUEST_NO_PATH_TO_ACCOM"));
}

OBL_TEST_CASE("guest_thoughts.a_known_text_moves_the_older_thoughts_back")
{
    Fixture f;
    FakeEnv env;
    f.guest->m_Thoughts[0] = 10, f.guest->m_Thoughts[1] = 20, f.guest->m_Thoughts[2] = 30;
    env.id = 77;
    OBL_CHECK_EQ(ThinkOf(*f.guest, kDetained, env), 77);
    OBL_CHECK_EQ(f.guest->m_Thoughts[0], 77);
    OBL_CHECK_EQ(f.guest->m_Thoughts[1], 10);
    OBL_CHECK_EQ(f.guest->m_Thoughts[2], 20);
}

OBL_TEST_CASE("guest_thoughts.an_unrecognized_text_changes_nothing_but_is_still_returned")
{
    Fixture f;
    FakeEnv env;
    env.rand = 1;
    env.missing = {"GUEST_THOUGHT_DETAINED2"};
    f.guest->m_Thoughts[0] = 10, f.guest->m_Thoughts[1] = 20, f.guest->m_Thoughts[2] = 30;
    // The building thoughts are handed the text table get string routine(-1) all the same.
    OBL_CHECK_EQ(ThinkOf(*f.guest, kDetained, env), -1);
    OBL_CHECK_EQ(f.guest->m_Thoughts[0], 10);
    OBL_CHECK_EQ(f.guest->m_Thoughts[1], 20);
    OBL_CHECK_EQ(f.guest->m_Thoughts[2], 30);
}

OBL_TEST_CASE("guest_thoughts.drowning_prefix_follows_the_gender")
{
    obl::tables::GuestDef def{};
    def.m_Gender = 0;
    OBL_CHECK_EQ(std::string(DrowningPrefix(def)), std::string("GUEST_THOUGHT_MALE_DROWNING"));
    def.m_Gender = 1;
    OBL_CHECK_EQ(std::string(DrowningPrefix(def)), std::string("GUEST_THOUGHT_FEMALE_DROWNING"));
    def.m_Gender = 2;
    OBL_CHECK_EQ(std::string(DrowningPrefix(def)), std::string("GUEST_THOUGHT_FEMALE_DROWNING"));
}

OBL_TEST_CASE("guest_thoughts.detained_for_nothing_takes_point_four_off_every_opinion")
{
    Fixture f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float input[7] = {1.0f, 0.5f, 0.4f, 0.39f, 0.0f, -0.2f, nan};
    for (int i = 0; i < 7; ++i) f.guest->m_opinion[i] = input[i];
    f.guest->m_moodIconTime = -3.0f;
    f.icon.m_frameIndex = 0;
    PenaliseDetainedForNothing(*f.guest);
    // The constant is the float 0.4f widened, so 0.4f - 0.4f is exactly zero and a bit more than 0.6 is left of 1.
    OBL_CHECK_EQ(f.guest->m_opinion[0], static_cast<float>(1.0 - static_cast<double>(0.4f)));
    OBL_CHECK_EQ(f.guest->m_opinion[1], static_cast<float>(0.5 - static_cast<double>(0.4f)));
    OBL_CHECK_EQ(f.guest->m_opinion[2], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[3], 0.0f); // below zero: held
    OBL_CHECK_EQ(f.guest->m_opinion[4], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[5], 0.0f);
    OBL_CHECK_EQ(f.guest->m_opinion[6], 0.0f); // an unordered pair counts as "below"
    OBL_CHECK_EQ(f.icon.m_frameIndex, 1);
    OBL_CHECK_EQ(f.guest->m_moodIconTime, 2.0f);
}

OBL_TEST_CASE("guest_thoughts.beach_closed_lowers_variety_without_a_bound")
{
    Fixture f;
    f.def->m_OpinionEffect[Variety][1] = 0.25f;
    for (float& opinion : f.guest->m_opinion) opinion = 0.1f;
    f.guest->m_moodIconTime = 0.7f; // the timer is not looked at
    PenaliseBeachClosed(*f.guest, *f.def);
    OBL_CHECK_EQ(f.guest->m_opinion[Variety], static_cast<float>(static_cast<double>(0.1f) - static_cast<double>(0.25f)));
    OBL_CHECK(f.guest->m_opinion[Variety] < 0.0f);
    for (int i = 0; i < 7; ++i)
        if (i != Variety) OBL_CHECK_EQ(f.guest->m_opinion[i], 0.1f);
    OBL_CHECK_EQ(f.icon.m_frameIndex, 1);
    OBL_CHECK_EQ(f.guest->m_moodIconTime, 2.0f);
}

OBL_TEST_CASE("guest_thoughts.ticker_is_due_only_after_more_than_twenty_seconds")
{
    OBL_CHECK(!NoPathTickerDue(100.0f, 80.0f)); // exactly 20: not due
    OBL_CHECK(NoPathTickerDue(100.0f, 79.5f));
    OBL_CHECK(!NoPathTickerDue(100.0f, 90.0f));
    OBL_CHECK(!NoPathTickerDue(100.0f, 200.0f)); // the clock went back (a loaded game): never
    OBL_CHECK(!NoPathTickerDue(100.0f, std::numeric_limits<float>::quiet_NaN()));
    OBL_CHECK(!NoPathTickerDue(std::numeric_limits<float>::quiet_NaN(), 0.0f));
    // Floats near 1e8 are 8 apart: 16 and 24 seconds are told apart, the difference is exact in double.
    OBL_CHECK(!NoPathTickerDue(100000000.0f, 99999984.0f));
    OBL_CHECK(NoPathTickerDue(100000000.0f, 99999976.0f));
}
