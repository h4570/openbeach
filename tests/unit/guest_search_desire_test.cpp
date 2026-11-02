#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/guest_search_desire.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using obl::object::Guest;
using namespace obl::tables;
using namespace obl::guest_search;

namespace {

// What the provider search answers for one family.
struct Answer {
    int32_t id = 0, slot = 0;
    uint8_t found = 0;
    int32_t broken = 0, closed = 0, beachClosed = 0;
    uint8_t retry = 0;
};

// The game as a script of answers; every call is written to `log` in order.
struct SearchEnv {
    std::string log;
    int32_t weather = 1;
    float delta = 0.02f, hour = 12.0f;
    bool badGuts = false, leave = false, bed = false, souvenirShop = true;
    std::deque<int> rands; // then 0 for ever
    std::vector<std::string> keys;
    std::set<std::string> missingTexts;
    std::map<int32_t, Answer> providers;
    guest_search::Provider provider{-1, -1};
    std::string family = "MEALS";
    std::vector<Message> posts;
    float sun[6] = {1, 2, 3, 40, 50, 6};
    float swim[6] = {11, 12, 13, 14, 15, 16};
    bool sea = true;
    float walk[2] = {70.5f, 80.5f};
    int32_t repeatDesire = -1;
    float repeatValue = -1.0f;
    const char* fatalName = nullptr;

    int32_t Weather() { return weather; }
    float GameDeltaTime() { return delta; }
    float DayTime() { return hour; }
    bool StartStomachIllness() { return add("BadGuts"), badGuts; }
    bool CheckLeaveResort() { return add("Leave"), leave; }
    bool CheckBedTime() { return add("Bed"), bed; }
    int Rand()
    {
        add("Rand");
        if (rands.empty()) return 0;
        const int value = rands.front();
        rands.pop_front();
        return value;
    }
    int32_t FindKey(const char* key)
    {
        keys.push_back(key);
        return missingTexts.count(key) ? -1 : 100 + static_cast<int32_t>(keys.size());
    }
    const char* RetryFamilyName() { return add("Family"), family.c_str(); }
    guest_search::Provider& Provider() { return provider; }
    int32_t FindProvider(
            int32_t* slot, uint8_t* found, int32_t* broken, int32_t* closed, int32_t* beachClosed, uint8_t* retry, int32_t wanted)
    {
        add("Find" + std::to_string(wanted));
        const Answer answer = providers.count(wanted) ? providers[wanted] : Answer{};
        *found = answer.found;
        *broken = answer.broken;
        *closed += answer.closed;
        *beachClosed += answer.beachClosed;
        *retry = answer.retry;
        if (answer.found || answer.closed || answer.broken || answer.beachClosed) *slot = answer.slot;
        return answer.id;
    }
    const char* BuildingKey(int32_t id) { return id == 7 ? "CAFE" : "OTHER"; }
    const char* CommodityName(int32_t id, int32_t slot) { return id == 7 && slot == 2 ? "BURGER" : "THING"; }
    int32_t BuildingTypeId(int32_t id) { return id * 10; }
    void WalkAlongPath() { add("Path"); }
    void PlayWalkAnimation() { add("Play"); }
    void BadVisitSound() { add("obl::audio::Sound"); }
    const float* FindSunbathePoint() { return add("SunPoint"), sun; }
    const float* FindSwimPoint(float, float, float) { return add("SwimPoint"), sea ? swim : nullptr; }
    const float* RandomWalkPoint() { return add("WalkPoint"), walk; }
    bool SouvenirShopAllowed() { return add("Shop"), souvenirShop; }
    void SetRepeatAction(int32_t desire, float value)
    {
        add("Repeat");
        repeatDesire = desire;
        repeatValue = value;
    }
    void Fatal(const char* name)
    {
        add("Fatal");
        fatalName = name;
    }
    void Post(const Message& message)
    {
        add("Post" + std::to_string(message.type) + ":" + std::to_string(message.data));
        posts.push_back(message);
    }
    void ApplyWalkAnimation() { add("Anim"); }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

struct Fixture {
    std::unique_ptr<Guest> guest = std::make_unique<Guest>();
    std::unique_ptr<obl::tables::GuestDef> def = std::make_unique<obl::tables::GuestDef>();
    obl::ui::SpriteInstance icon{};
    Fixture()
    {
        obl::unit::FillObject<Guest>(*guest);
        *def = GuestDef{};
        std::memset(&icon, 0, sizeof(icon));
        icon.m_frameIndex = -1;
        guest->m_id = 77;
        guest->m_definition = def.get();
        guest->m_moodIcon = &icon;
        guest->m_StayingAt = -1;
        guest->m_person[kWalkingPerson] = 1.0f;
        guest->m_opinion[Variety] = 0.5f;
        guest->m_HasSatDown = 1;
        guest->m_WorldXYZ[0] = 300.0f;
        guest->m_WorldXYZ[1] = 400.0f;
        guest->m_WorldXYZ[2] = 5.0f;
        std::strcpy(guest->m_Name, "Gail");
        std::strcpy(guest->m_WalkAnimation, "d_walk_normal");
        std::strcpy(guest->m_StandAnimation, "d_stand_normal");
        for (int i = 0; i < 7; ++i) {
            def->m_OpinionEffect[i][0] = 0.125f;
            def->m_OpinionEffect[i][1] = 0.25f;
        }
        for (int& max : def->m_Max) max = 4;
    }
    Guest& Visitor() { return *guest; }
};

} // namespace

OBL_TEST_CASE("guest_search.a_frame_without_game_time_only_copies_the_missed_flag")
{
    Fixture f;
    f.Visitor().m_missedDesire = 5;
    SearchEnv env;
    env.delta = 0.0f;
    OBL_CHECK(!SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string());
    OBL_CHECK_EQ(int(f.Visitor().m_HasSatDown), 0);
    OBL_CHECK_EQ(int(f.Visitor().m_missedDesireLast), 1);
    env.delta = std::numeric_limits<float>::quiet_NaN(); // "not above 0" includes NaN
    f.Visitor().m_missedDesire = 0;
    OBL_CHECK(!SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(int(f.Visitor().m_missedDesireLast), 0);
}

OBL_TEST_CASE("guest_search.illness_leaving_and_bed_time_take_the_guest_over_in_that_order")
{
    Fixture f;
    f.Visitor().m_desires[5] = 0.9f;
    SearchEnv ill;
    ill.badGuts = true;
    OBL_CHECK(SearchDesire(f.Visitor(), ill));
    OBL_CHECK_EQ(ill.log, std::string("BadGuts"));
    SearchEnv leaving;
    leaving.leave = true;
    OBL_CHECK(SearchDesire(f.Visitor(), leaving));
    OBL_CHECK_EQ(leaving.log, std::string("BadGuts Leave"));
    SearchEnv bed;
    bed.bed = true;
    bed.weather = 3; // the weather thought comes after the bed check
    OBL_CHECK(SearchDesire(f.Visitor(), bed));
    OBL_CHECK_EQ(bed.log, std::string("BadGuts Leave Bed"));
    OBL_CHECK(bed.keys.empty());
}

OBL_TEST_CASE("guest_search.strongest_desire_is_strictly_greater_and_the_bladder_needs_its_threshold")
{
    float desires[kDesireCount] = {};
    desires[3] = 0.5f;
    desires[8] = 0.5f; // a tie keeps the first
    desires[Relief] = 0.6f; // not above the threshold
    float best = 0.0f;
    uint32_t desire = kNoDesire;
    PickStrongest(desires, best, desire);
    OBL_CHECK_EQ(desire, 3u);
    OBL_CHECK_EQ(best, 0.5f);

    desires[Relief] = 0.61f;
    best = 0.0f;
    desire = kNoDesire;
    PickStrongest(desires, best, desire);
    OBL_CHECK_EQ(desire, uint32_t(Relief));

    float none[kDesireCount] = {};
    none[2] = -1.0f;
    best = 0.0f;
    desire = kNoDesire;
    PickStrongest(none, best, desire);
    OBL_CHECK_EQ(desire, kNoDesire);

    // An unordered compare counts as "below": a NaN desire wins, and then so does the next one, whatever its value.
    float odd[kDesireCount] = {};
    odd[1] = 0.9f;
    odd[4] = std::numeric_limits<float>::quiet_NaN();
    best = 0.0f;
    desire = kNoDesire;
    PickStrongest(odd, best, desire);
    OBL_CHECK_EQ(desire, 5u);
    OBL_CHECK_EQ(best, 0.0f);
}

OBL_TEST_CASE("guest_search.small_rules")
{
    OBL_CHECK_EQ(DesireOfFamily("SWIMFUN"), uint32_t(SwimFun));
    OBL_CHECK_EQ(DesireOfFamily("ACCOMDTN"), 0u);
    OBL_CHECK_EQ(DesireOfFamily("swimfun"), uint32_t(SwimFun)); // case does not matter
    OBL_CHECK_EQ(DesireOfFamily("BEER"), kNoDesire);
    OBL_CHECK_EQ(RepeatCount(4, 0.8f), 3);
    OBL_CHECK_EQ(RepeatCount(4, 0.1f), 1); // 0 becomes 1
    OBL_CHECK_EQ(RepeatCount(4, 0.0f), 1);
    OBL_CHECK_EQ(RepeatCount(4, -1.0f), -4); // the floor is an unsigned test
    OBL_CHECK_EQ(RepeatCount(-1, 0.5f), 0x7fffffff); // the maximum is read as unsigned
    OBL_CHECK(Daytime(9.5f));
    OBL_CHECK(!Daytime(9.0f));
    OBL_CHECK(!Daytime(18.0f));
    OBL_CHECK(!Daytime(std::numeric_limits<float>::quiet_NaN()));
    OBL_CHECK_EQ(std::string(WeatherThought(0)), std::string("GUEST_THOUGHT_WEATHER_HEAT_WAVE"));
    OBL_CHECK_EQ(std::string(WeatherThought(5)), std::string("GUEST_THOUGHT_WEATHER_THUNDERSTORM"));
    OBL_CHECK_EQ(std::string(WeatherThought(9)), std::string());
    OBL_CHECK_EQ(std::string(ActivityThought(ActPee)), std::string("GUEST_THOUGHT_HEAD_ACT_PEEING"));
    OBL_CHECK_EQ(std::string(ActivityThought(5)), std::string());
}

OBL_TEST_CASE("guest_search.no_desire_tells_a_walking_guest_to_carry_on_and_is_fatal_for_another")
{
    Fixture f;
    SearchEnv env;
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Post3:57"));
    OBL_CHECK_EQ(env.posts[0].sender, 77);
    OBL_CHECK_EQ(env.posts[0].receiver, 77);
    OBL_CHECK_EQ(env.posts[0].flags, 0);
    f.Visitor().m_person[kWalkingPerson] = 0.0f;
    SearchEnv fatal;
    OBL_CHECK(SearchDesire(f.Visitor(), fatal));
    OBL_CHECK_EQ(fatal.log, std::string("BadGuts Leave Bed Fatal"));
    OBL_CHECK_EQ(std::string(fatal.fatalName), std::string("Gail"));
}

OBL_TEST_CASE("guest_search.a_stroll_walks_to_a_random_point_and_is_remembered_as_an_activity")
{
    Fixture f;
    f.Visitor().m_desires[Stroll] = 0.8f;
    f.Visitor().m_activity = 0;
    f.Visitor().m_ArrayPos = 0;
    SearchEnv env;
    env.rands = {4};
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed WalkPoint Post3:13 Rand Post8:80 Anim"));
    OBL_CHECK_EQ(f.Visitor().m_walkpoints[0][0], 70.5f);
    OBL_CHECK_EQ(f.Visitor().m_walkpoints[0][1], 80.5f);
    OBL_CHECK_EQ(env.posts[1].flags, 70);
    OBL_CHECK_EQ(f.Visitor().m_desired_commod, 0);
    OBL_CHECK_EQ(int(f.Visitor().m_activity), 1);
    OBL_CHECK_EQ(f.Visitor().m_RepeatAction, 3); // 4 * 0.8
    OBL_CHECK_EQ(f.Visitor().m_DestObjectID, -1);
    OBL_CHECK_EQ(env.keys[0], std::string("GUEST_THOUGHT_HEAD_ACT_STROLL2")); // no separator before the digit
    OBL_CHECK_EQ(f.Visitor().m_Thoughts[0], 101);
    OBL_CHECK_EQ(int(f.Visitor().m_lastActivity[0]), 1);
    OBL_CHECK_EQ(f.Visitor().m_LastID[0], 0);
    OBL_CHECK_EQ(f.Visitor().m_ArrayPos, 1);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 0.5f); // a stroll is no variety
    OBL_CHECK_EQ(f.Visitor().m_desires[Stroll], 0.8f); // only the copy is cleared
    OBL_CHECK_EQ(std::string(f.Visitor().m_CurrAnimation), std::string("d_walk_normal"));
}

OBL_TEST_CASE("guest_search.bad_weather_skips_the_stroll_and_takes_the_next_desire")
{
    Fixture f;
    f.Visitor().m_desires[Stroll] = 0.8f;
    f.Visitor().m_desires[Fight] = 0.3f;
    SearchEnv env;
    env.weather = 4;
    env.missingTexts = {"GUEST_THOUGHT_WEATHER_HEAVYRAIN1"};
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    // A fight sends neither the "heading" state nor a walking goal.
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Rand Post3:31 Rand Anim"));
    OBL_CHECK_EQ(env.keys[0], std::string("GUEST_THOUGHT_WEATHER_HEAVYRAIN1"));
    OBL_CHECK_EQ(env.keys[1], std::string("GUEST_THOUGHT_HEAD_ACT_FIGHTING1"));
    OBL_CHECK_EQ(f.Visitor().m_desired_commod, int32_t(ActFight));
    OBL_CHECK_EQ(f.Visitor().m_LastID[0], 3);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 0.5f);
}

OBL_TEST_CASE("guest_search.a_provider_sends_the_guest_to_the_building_and_raises_variety")
{
    Fixture f;
    f.Visitor().m_desires[5] = 0.7f;
    f.Visitor().m_opinion[Variety] = 0.95f;
    f.Visitor().m_ArrayPos = 1;
    f.Visitor().m_activity = 1; // cleared by the pick
    SearchEnv env;
    env.providers[5] = {7, 2, 1};
    env.rands = {2};
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Find5 Repeat Post3:13 Post9:7 Rand Anim"));
    OBL_CHECK_EQ(env.repeatDesire, 5);
    OBL_CHECK_EQ(env.repeatValue, 0.7f);
    OBL_CHECK_EQ(env.posts[1].flags, 1);
    OBL_CHECK_EQ(f.Visitor().m_CurID, 7);
    OBL_CHECK_EQ(f.Visitor().m_CurTypeID, 70);
    OBL_CHECK_EQ(f.Visitor().m_desired_commod, 2);
    OBL_CHECK_EQ(int(f.Visitor().m_lastActivity[1]), 0);
    OBL_CHECK_EQ(f.Visitor().m_LastID[1], 7);
    OBL_CHECK_EQ(f.Visitor().m_ArrayPos, 0);
    OBL_CHECK_EQ(env.keys[0], std::string("GUEST_THOUGHT_HEAD_BURGER3"));
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 1.0f); // 0.95 + 0.125, clamped at the end
    OBL_CHECK_EQ(env.provider.id, 7);
    OBL_CHECK_EQ(env.provider.slot, 2);
}

OBL_TEST_CASE("guest_search.a_closed_provider_makes_the_guest_walk_away_sad")
{
    Fixture f;
    f.Visitor().m_desires[5] = 0.7f;
    f.Visitor().m_desires[8] = 0.6f;
    f.Visitor().m_opinion[Variety] = 0.6f;
    SearchEnv env;
    env.providers[5] = {7, 2, 0, 0, 2, 0, 0}; // two closed
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Find5 Rand Path Play obl::audio::Sound"));
    OBL_CHECK_EQ(env.keys[0], std::string("GUEST_THOUGHT_CLOSED_CAFE1"));
    OBL_CHECK_EQ(int(f.Visitor().m_missedPurchase), 1);
    OBL_CHECK_EQ(int(f.Visitor().m_lastBad), 0); // set, then consumed by the mood icon
    OBL_CHECK_EQ(f.icon.m_frameIndex, int(kMoodIconSad));
    OBL_CHECK_EQ(f.Visitor().m_moodIconTime, 2.0f);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 0.0f); // 0.6 - 2 * 0.25 - 0.25, floored
    OBL_CHECK_EQ(f.Visitor().m_desires[5], 0.0f);
    OBL_CHECK_EQ(f.Visitor().m_desires[8], 0.6f);
    OBL_CHECK(env.posts.empty());
}

OBL_TEST_CASE("guest_search.refusal_thoughts_name_the_first_counter_that_is_set")
{
    const auto key = [](Answer answer) {
        Fixture f;
        f.Visitor().m_desires[5] = 0.7f;
        SearchEnv env;
        env.providers[5] = answer;
        SearchDesire(f.Visitor(), env);
        return env.keys.at(0);
    };
    OBL_CHECK_EQ(key({7, 2, 0, 1, 1, 1, 0}), std::string("GUEST_THOUGHT_CLOSED_CAFE1"));
    OBL_CHECK_EQ(key({7, 2, 0, 1, 0, 1, 0}), std::string("GUEST_THOUGHT_BROKEN_CAFE1"));
    OBL_CHECK_EQ(key({7, 2, 0, 0, 0, 1, 0}), std::string("GUEST_THOUGHT_BEACH_CLOSED_BURGER1"));
    OBL_CHECK_EQ(key({}), std::string("GUEST_THOUGHT_MISSED_MEALS1"));
}

OBL_TEST_CASE("guest_search.a_good_last_visit_turns_into_the_happy_icon_unless_one_is_showing")
{
    Fixture f;
    f.Visitor().m_lastGood = 1;
    SettleVisitMood(f.Visitor());
    OBL_CHECK_EQ(f.icon.m_frameIndex, int(kMoodIconHappy));
    OBL_CHECK_EQ(int(f.Visitor().m_lastGood), 0);
    f.Visitor().m_lastBad = 1;
    f.icon.m_frameIndex = 9;
    f.Visitor().m_moodIconTime = 0.5f;
    SettleVisitMood(f.Visitor());
    OBL_CHECK_EQ(f.icon.m_frameIndex, 9); // still showing
    OBL_CHECK_EQ(int(f.Visitor().m_lastBad), 0);
    SettleVisitMood(f.Visitor()); // neither flag: nothing
    OBL_CHECK_EQ(f.Visitor().m_moodIconTime, 0.5f);
}

OBL_TEST_CASE("guest_search.come_back_later_clears_the_desire_and_moves_on_but_keeps_a_headache")
{
    Fixture f;
    f.Visitor().m_desires[Pills] = 0.9f;
    f.Visitor().m_desires[5] = 0.7f;
    f.Visitor().m_desires[Stroll] = 0.2f;
    SearchEnv env;
    env.providers[Pills] = {0, 0, 0, 0, 0, 0, 1};
    env.providers[5] = {0, 0, 0, 0, 0, 0, 1};
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Find4 Rand Find5 WalkPoint Post3:13 Rand Post8:80 Anim"));
    OBL_CHECK_EQ(env.keys[0], std::string("GUEST_THOUGHT_HEADACHE_LATER1"));
    OBL_CHECK_EQ(f.Visitor().m_desires[Pills], 0.9f);
    OBL_CHECK_EQ(f.Visitor().m_desires[5], 0.0f);
}

OBL_TEST_CASE("guest_search.souvenirs_send_the_guest_home_without_a_shop_or_a_provider")
{
    Fixture f;
    f.Visitor().m_desires[Souvenir] = 0.9f;
    SearchEnv banned;
    banned.souvenirShop = false;
    OBL_CHECK(SearchDesire(f.Visitor(), banned));
    OBL_CHECK_EQ(banned.log, std::string("BadGuts Leave Bed Find10 Shop Rand Post3:41"));
    OBL_CHECK_EQ(banned.keys[0], std::string("GUEST_THOUGHT_GO_HOME1"));

    // With a shop in the game but none to go to, the guest walks away first and still goes home.
    SearchEnv none;
    OBL_CHECK(SearchDesire(f.Visitor(), none));
    OBL_CHECK_EQ(none.log, std::string("BadGuts Leave Bed Find10 Shop Rand Path Play obl::audio::Sound Rand Post3:41"));
    OBL_CHECK_EQ(none.keys[0], std::string("GUEST_THOUGHT_MISSED_SOUVENIR1"));
    OBL_CHECK_EQ(f.Visitor().m_desires[Souvenir], 0.9f);
}

OBL_TEST_CASE("guest_search.sunbathing_prefers_a_lounger_three_times_in_four")
{
    Fixture f;
    f.Visitor().m_desires[Sunbathe] = 0.5f;
    SearchEnv lounger;
    lounger.providers[Sunbathe] = {7, 2, 1};
    lounger.rands = {1};
    OBL_CHECK(SearchDesire(f.Visitor(), lounger));
    OBL_CHECK_EQ(lounger.log, std::string("BadGuts Leave Bed Find12 Rand Post3:13 Post9:7 Rand Anim"));
    OBL_CHECK_EQ(f.Visitor().m_RepeatAction, 2);
    OBL_CHECK_EQ(int(f.Visitor().m_activity), 0);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 0.625f);

    Fixture g;
    g.Visitor().m_desires[Sunbathe] = 0.5f;
    SearchEnv beach;
    beach.providers[Sunbathe] = {7, 2, 1};
    beach.rands = {8};
    OBL_CHECK(SearchDesire(g.Visitor(), beach));
    OBL_CHECK_EQ(beach.log, std::string("BadGuts Leave Bed Find12 Rand SunPoint Post3:13 Rand Post8:50 Anim"));
    OBL_CHECK_EQ(g.Visitor().m_walkpoints[0][0], 40.0f);
    OBL_CHECK_EQ(g.Visitor().m_walkpoints[0][1], 50.0f);
    OBL_CHECK_EQ(g.Visitor().m_desired_commod, int32_t(ActSunbathe));
    OBL_CHECK_EQ(g.Visitor().m_opinion[Variety], 0.5f);

    // At night, or in rain, the desire is passed over.
    Fixture h;
    h.Visitor().m_desires[Sunbathe] = 0.5f;
    SearchEnv night;
    night.hour = 20.0f;
    OBL_CHECK(SearchDesire(h.Visitor(), night));
    OBL_CHECK_EQ(night.log, std::string("BadGuts Leave Bed Post3:57"));
    SearchEnv rain;
    rain.weather = 2;
    OBL_CHECK(SearchDesire(h.Visitor(), rain));
    OBL_CHECK_EQ(rain.log, std::string("BadGuts Leave Bed Rand Post3:57"));
}

OBL_TEST_CASE("guest_search.swimming_without_a_reachable_sea_costs_every_opinion")
{
    Fixture f;
    f.Visitor().m_desires[SwimFun] = 0.5f;
    for (float& opinion : f.Visitor().m_opinion) opinion = 0.75f;
    f.Visitor().m_opinion[Beauty] = 0.1f;
    f.Visitor().m_opinion[Safety] = 2.0f; // no ceiling here
    SearchEnv env;
    env.sea = false;
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Find13 SwimPoint Rand Path Play obl::audio::Sound"));
    OBL_CHECK_EQ(env.keys[0], std::string("GUEST_THOUGHT_MISSED_SWIMFUN1"));
    OBL_CHECK_EQ(f.Visitor().m_opinion[Beauty], 0.0f);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 0.5f);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Safety], 1.75f);
    OBL_CHECK_EQ(f.Visitor().m_desires[SwimFun], 0.0f);
}

OBL_TEST_CASE("guest_search.swimming_goes_to_the_sea_by_day_and_to_the_pool_otherwise")
{
    Fixture f;
    f.Visitor().m_desires[SwimFun] = 0.5f;
    SearchEnv sea;
    OBL_CHECK(SearchDesire(f.Visitor(), sea));
    OBL_CHECK_EQ(sea.log, std::string("BadGuts Leave Bed Find13 SwimPoint Post3:13 Rand Post8:12 Anim"));
    OBL_CHECK_EQ(f.Visitor().m_walkpoints[1][0], 14.0f);
    OBL_CHECK_EQ(f.Visitor().m_walkpoints[1][1], 15.0f);
    OBL_CHECK_EQ(f.Visitor().m_desired_commod, int32_t(ActSwim));
    OBL_CHECK_EQ(f.Visitor().m_RepeatAction, 2);

    Fixture g;
    g.Visitor().m_desires[SwimFun] = 0.5f;
    SearchEnv pool;
    pool.providers[SwimFun] = {7, 2, 1};
    pool.rands = {3};
    OBL_CHECK(SearchDesire(g.Visitor(), pool));
    OBL_CHECK_EQ(pool.log, std::string("BadGuts Leave Bed Find13 Rand Post3:13 Post9:7 Rand Anim"));

    // A refused pool (no "come back later") is a walk away with the ceiling applied on the double.
    Fixture h;
    h.Visitor().m_desires[SwimFun] = 0.5f;
    h.Visitor().m_opinion[Variety] = 0.5f;
    h.def->m_OpinionEffect[Variety][1] = -1.0f;
    SearchEnv refused;
    refused.providers[SwimFun] = {7, 2, 0, 1, 0, 0, 0};
    OBL_CHECK(SearchDesire(h.Visitor(), refused));
    OBL_CHECK_EQ(refused.log, std::string("BadGuts Leave Bed Find13 Rand Path Play obl::audio::Sound"));
    OBL_CHECK_EQ(refused.keys[0], std::string("GUEST_THOUGHT_BROKEN_CAFE1"));
    OBL_CHECK_EQ(h.Visitor().m_opinion[Variety], 1.0f); // 0.5 + 1 + 1
    OBL_CHECK_EQ(h.Visitor().m_desires[SwimFun], 0.0f);
}

OBL_TEST_CASE("guest_search.a_full_bladder_without_a_toilet_is_relieved_on_the_spot_and_walks_desperately")
{
    Fixture f;
    f.Visitor().m_desires[Relief] = 0.9f;
    SearchEnv env;
    env.rands = {0, 5};
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Find14 Post3:13 Rand Post8:400 Rand Anim"));
    OBL_CHECK_EQ(f.Visitor().m_desired_commod, int32_t(ActPee));
    OBL_CHECK_EQ(f.Visitor().m_walkpoints[1][2], 5.0f);
    OBL_CHECK_EQ(std::string(f.Visitor().m_WalkAnimation), std::string("D_WALK_DESPERATE"));
    OBL_CHECK_EQ(std::string(f.Visitor().m_StandAnimation), std::string("D_STAND_DESPERATE1")); // an odd draw
    OBL_CHECK_EQ(std::string(f.Visitor().m_CurrAnimation), std::string("D_WALK_DESPERATE"));

    // The next search, for something else, puts the normal walk back.
    f.Visitor().m_desires[Relief] = 0.0f;
    f.Visitor().m_desires[Stroll] = 0.5f;
    SearchEnv next;
    OBL_CHECK(SearchDesire(f.Visitor(), next));
    OBL_CHECK_EQ(std::string(f.Visitor().m_WalkAnimation), std::string("D_WALK_NORMAL"));
    OBL_CHECK_EQ(std::string(f.Visitor().m_StandAnimation), std::string("D_STAND_NORMAL"));
}

OBL_TEST_CASE("guest_search.water_fun_and_parties_are_put_off_in_bad_weather_without_asking_a_provider")
{
    Fixture f;
    f.Visitor().m_desires[WaterFun] = 0.9f;
    f.Visitor().m_desires[PartyFun] = 0.8f;
    SearchEnv env;
    env.weather = 3;
    env.providers[PartyFun] = {7, 2, 1};
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    // Light rain: no water fun (above 2), but the party (bad from 4) is on.
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Rand Find6 Repeat Post3:13 Post9:7 Rand Anim"));
    OBL_CHECK_EQ(f.Visitor().m_desires[WaterFun], 0.0f);
}

OBL_TEST_CASE("guest_search.after_a_miss_the_family_of_the_last_building_is_tried_again_with_no_strength")
{
    Fixture f;
    f.Visitor().m_missedDesire = 1;
    f.Visitor().m_CurID = 3;
    f.Visitor().m_desires[Stroll] = 0.9f;
    SearchEnv env;
    env.family = "MEALS";
    env.providers[5] = {7, 2, 1};
    OBL_CHECK(SearchDesire(f.Visitor(), env));
    OBL_CHECK_EQ(env.log, std::string("BadGuts Leave Bed Family Find5 Repeat Post3:13 Post9:7 Rand Anim"));
    OBL_CHECK_EQ(env.repeatValue, 0.0f);
    OBL_CHECK_EQ(int(f.Visitor().m_missedDesire), 0);
    OBL_CHECK_EQ(int(f.Visitor().m_missedDesireLast), 1);

    // Family 0 falls back to the strongest desire; an unknown family leaves the guest without one.
    Fixture g;
    g.Visitor().m_missedDesire = 1;
    g.Visitor().m_CurID = 3;
    g.Visitor().m_desires[Stroll] = 0.9f;
    SearchEnv first;
    first.family = "ACCOMDTN";
    OBL_CHECK(SearchDesire(g.Visitor(), first));
    OBL_CHECK_EQ(first.log, std::string("BadGuts Leave Bed Family WalkPoint Post3:13 Rand Post8:80 Anim"));
    Fixture h;
    h.Visitor().m_missedDesire = 1;
    h.Visitor().m_CurID = 3;
    h.Visitor().m_desires[Stroll] = 0.9f;
    SearchEnv unrecognized;
    unrecognized.family = "BEER";
    OBL_CHECK(SearchDesire(h.Visitor(), unrecognized));
    OBL_CHECK_EQ(unrecognized.log, std::string("BadGuts Leave Bed Family Post3:57"));

    // The hotel the guest stays at does not count as a missed building.
    Fixture i;
    i.Visitor().m_missedDesire = 1;
    i.Visitor().m_CurID = 3;
    i.Visitor().m_StayingAt = 3;
    i.Visitor().m_desires[Stroll] = 0.9f;
    SearchEnv hotel;
    OBL_CHECK(SearchDesire(i.Visitor(), hotel));
    OBL_CHECK_EQ(hotel.log, std::string("BadGuts Leave Bed WalkPoint Post3:13 Rand Post8:80 Anim"));
    OBL_CHECK_EQ(int(i.Visitor().m_missedDesire), 1);
}

OBL_TEST_CASE("guest_search.vandalism_and_flirting_end_the_search_with_a_state_change_and_a_thought")
{
    Fixture f;
    f.Visitor().m_desires[Vandal] = 0.9f;
    SearchEnv vandal;
    OBL_CHECK(SearchDesire(f.Visitor(), vandal));
    OBL_CHECK_EQ(vandal.log, std::string("BadGuts Leave Bed Post3:51 Rand"));
    OBL_CHECK_EQ(vandal.keys[0], std::string("GUEST_THOUGHT_HEAD_ACT_VANDALISING1"));
    OBL_CHECK_EQ(f.Visitor().m_desired_commod, int32_t(ActVandal));
    OBL_CHECK_EQ(f.Visitor().m_DestObjectID, -1);

    Fixture g;
    g.Visitor().m_desires[Flirt] = 0.9f;
    g.Visitor().m_DestObjectID = 5;
    SearchEnv flirt;
    OBL_CHECK(SearchDesire(g.Visitor(), flirt));
    OBL_CHECK_EQ(flirt.log, std::string("BadGuts Leave Bed Post3:34 Rand"));
    OBL_CHECK_EQ(g.Visitor().m_DestObjectID, 5); // flirting keeps the destination
    OBL_CHECK_EQ(g.Visitor().m_desired_commod, int32_t(ActFlirt));

    // Scared and "hard sex" only clear the guest's own desire.
    Fixture h;
    h.Visitor().m_desires[Scared] = 0.9f;
    h.Visitor().m_desires[HardSex] = 0.8f;
    SearchEnv none;
    OBL_CHECK(SearchDesire(h.Visitor(), none));
    OBL_CHECK_EQ(none.log, std::string("BadGuts Leave Bed Post3:57"));
    OBL_CHECK_EQ(h.Visitor().m_desires[Scared], 0.0f);
    OBL_CHECK_EQ(h.Visitor().m_desires[HardSex], 0.0f);
}

OBL_TEST_CASE("guest_search.refusals_cost_variety_per_provider_with_the_floor_on_the_double")
{
    Fixture f;
    f.Visitor().m_opinion[Variety] = 0.6f;
    VarietyPenalty(f.Visitor(), 2);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], static_cast<float>(static_cast<double>(0.6f) - 2.0 * 0.25));
    VarietyPenalty(f.Visitor(), 3);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 0.0f);
    f.Visitor().m_opinion[Variety] = std::numeric_limits<float>::quiet_NaN();
    VarietyPenalty(f.Visitor(), 1);
    OBL_CHECK_EQ(f.Visitor().m_opinion[Variety], 0.0f); // NaN is "below"
    float opinion = 1.5f;
    ClampOpinion(opinion);
    OBL_CHECK_EQ(opinion, 1.0f);
    opinion = -0.5f;
    ClampOpinion(opinion);
    OBL_CHECK_EQ(opinion, 0.0f);
}
