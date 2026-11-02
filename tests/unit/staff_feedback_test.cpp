#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"

#include "game/objects/staff_update_feedback.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>

using namespace obl;
using namespace obl::layout;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::Mechanic;
using obl::object::Rep;
using obl::object::SecurityGuard;

namespace {

// The game as a script of answers; every call the method makes is written to `log` in order, as `Key(4) Add(K4,id,0xb) ...`.
struct FeedbackEnv {
    std::string log;
    uint32_t messages[16] = {}; // feedback::Feedback::StaffMessages by staff type; MarkStaffMessage adds to it
    uint32_t urgentJobs = 0;
    uint32_t guests = 0;
    std::map<std::string, int32_t> named;
    int32_t toiletCount = 0;
    std::set<uint32_t> noKey; // flags the profession has no text for
    std::deque<std::string> keys;

    const char* Key(uint32_t flags)
    {
        add("Key(" + hex(flags) + ")");
        if (noKey.count(flags)) return nullptr;
        keys.push_back("K" + hex(flags));
        return keys.back().c_str();
    }
    uint32_t StaffMessages(int type)
    {
        add("Get(" + hex(static_cast<uint32_t>(type)) + ")");
        return messages[type];
    }
    void Mark(uint32_t bits, int type)
    {
        add("Mark(" + hex(bits) + "," + hex(static_cast<uint32_t>(type)) + ")");
        messages[type] |= bits;
    }
    void Add(const char* key, int32_t id, uint32_t type)
    {
        add(std::string("Add(") + (key ? key : "null") + "," + std::to_string(id) + "," + hex(type) + ")");
    }
    void Push(uint32_t flags) { add("Push(" + hex(flags) + ")"); }
    void Blink(const char* icon) { add(std::string("Blink(") + icon + ")"); }
    uint32_t RepairJobs()
    {
        add("Repair");
        return urgentJobs;
    }
    int32_t CountByName(const char* name)
    {
        add(std::string("Count(") + name + ")");
        return named[name];
    }
    int32_t CountBuildings(BuildingBehavior behavior)
    {
        add(std::string("Buildings(") + BuildingBehaviorName(behavior) + ")");
        return toiletCount;
    }
    uint32_t CountGuests()
    {
        add("Guests");
        return guests;
    }

    static std::string hex(uint32_t value)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%x", value);
        return text;
    }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

template<class T> std::unique_ptr<T> Member(float happiness = 1.0f)
{
    auto member = std::make_unique<T>();
    obl::unit::FillObject<T>(*member);
    member->m_id = 77;
    member->m_happiness = happiness;
    return member;
}

} // namespace

OBL_TEST_CASE("staff_feedback.gate_blocks_everything_while_a_requirement_is_pending")
{
    auto builder = Member<Builder>(0.0f);
    builder->m_RequiredFeedback = 1;
    FeedbackEnv env;
    staff::UpdateBuilderFeedback(*builder, env);
    OBL_CHECK_EQ(env.log, std::string());
    OBL_CHECK_EQ(builder->m_FeedbackStatus, 0);
}

OBL_TEST_CASE("staff_feedback.builder_announces_quota_and_woken_up_without_conditions")
{
    auto builder = Member<Builder>();
    FeedbackEnv env;
    staff::UpdateBuilderFeedback(*builder, env);
    OBL_CHECK_EQ(env.log, std::string("Key(4) Add(K4,77,b) Push(4) Blink(BuilderIcon) Key(2) Add(K2,77,b) Push(2) Blink(BuilderIcon)"));
    OBL_CHECK_EQ(builder->m_FeedbackStatus, 6);
    OBL_CHECK_EQ(builder->m_CurrentFeedback, 2);
    OBL_CHECK_EQ(builder->m_RequiredFeedback, 0);
    env.log.clear();
    staff::UpdateBuilderFeedback(*builder, env); // the bits were tested: nothing more until a script clears them
    OBL_CHECK_EQ(env.log, std::string());
}

OBL_TEST_CASE("staff_feedback.builder_unhappy_leaves_its_bit_in_the_flags_of_the_next_texts")
{
    auto builder = Member<Builder>(0.5f);
    FeedbackEnv env;
    staff::UpdateBuilderFeedback(*builder, env);
    OBL_CHECK_EQ(env.log,
            std::string("Key(100000) Add(K100000,77,7) Push(800000) Key(800004) Add(K800004,77,b) Push(800004) Blink(BuilderIcon) "
                        "Key(2) Add(K2,77,b) Push(2) Blink(BuilderIcon)"));
    OBL_CHECK_EQ(builder->m_FeedbackStatus, 0x800006);
    OBL_CHECK_EQ(builder->m_RequiredFeedback, 0); // the first announcement consumed the requirement the unhappy block had set
}

OBL_TEST_CASE("staff_feedback.unhappy_limit_is_the_float_0_6_and_nan_counts_as_unhappy")
{
    for (float happiness : {0.6f, 0.61f, 1.0f, 2.0f}) {
        auto cleaner = Member<Cleaner>(happiness);
        cleaner->m_FeedbackStatus = 6 | 0x400000;
        FeedbackEnv env;
        staff::UpdateCleanerFeedback(*cleaner, env);
        OBL_CHECK_EQ(env.log, std::string());
    }
    for (float happiness : {0.59999996f, 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::infinity()}) {
        auto cleaner = Member<Cleaner>(happiness);
        cleaner->m_FeedbackStatus = 6 | 0x400000;
        FeedbackEnv env;
        staff::UpdateCleanerFeedback(*cleaner, env);
        OBL_CHECK_EQ(env.log, std::string("Key(100000) Add(K100000,77,7) Push(800000)"));
        OBL_CHECK_EQ(cleaner->m_RequiredFeedback, 0x800000);
        OBL_CHECK_EQ(cleaner->m_FeedbackStatus, 0x800000 | 6 | 0x400000);
    }
}

OBL_TEST_CASE("staff_feedback.unhappy_message_is_added_even_without_a_text")
{
    auto mechanic = Member<Mechanic>(0.1f);
    mechanic->m_FeedbackStatus = 2 | 0x400000;
    FeedbackEnv env;
    env.noKey.insert(0x100000);
    staff::UpdateMechanicFeedback(*mechanic, env);
    OBL_CHECK_EQ(env.log, std::string("Key(100000) Add(null,77,7) Push(800000)"));
}

OBL_TEST_CASE("staff_feedback.cleaner_without_toilets_tells_the_player_once")
{
    auto cleaner = Member<Cleaner>();
    cleaner->m_FeedbackStatus = 2 | 0x400000;
    FeedbackEnv env;
    staff::UpdateCleanerFeedback(*cleaner, env);
    OBL_CHECK_EQ(env.log, std::string("Get(b) Buildings(toilet) Mark(4,b) Key(4) Add(K4,77,c) Push(4) Blink(CleanerIcon)"));
    OBL_CHECK_EQ(cleaner->m_FeedbackStatus, 2 | 4 | 0x400000);
    OBL_CHECK_EQ(cleaner->m_CurrentFeedback, 4);
    OBL_CHECK_EQ(env.messages[0xb], 4u);

    // Already told to the player: the status bit is spent and nothing else happens (the toilet count is not even asked).
    auto again = Member<Cleaner>();
    again->m_FeedbackStatus = 2 | 0x400000;
    FeedbackEnv told;
    told.messages[0xb] = 4;
    staff::UpdateCleanerFeedback(*again, told);
    OBL_CHECK_EQ(told.log, std::string("Get(b)"));
    OBL_CHECK_EQ(again->m_FeedbackStatus, 2 | 4 | 0x400000);

    // With a toilet in the park nothing is announced, but the bit is spent as well.
    auto park = Member<Cleaner>();
    park->m_FeedbackStatus = 2 | 0x400000;
    FeedbackEnv toilets;
    toilets.toiletCount = 1;
    staff::UpdateCleanerFeedback(*park, toilets);
    OBL_CHECK_EQ(toilets.log, std::string("Get(b) Buildings(toilet)"));
    OBL_CHECK_EQ(park->m_FeedbackStatus, 2 | 4 | 0x400000);
}

OBL_TEST_CASE("staff_feedback.cleaner_busy_toilet_and_cant_do_job_clear_their_flags")
{
    auto cleaner = Member<Cleaner>();
    cleaner->m_FeedbackStatus = 4;
    cleaner->m_BusyToilet = -3;
    cleaner->m_cantDoJob = 5;
    FeedbackEnv env;
    staff::UpdateCleanerFeedback(*cleaner, env);
    OBL_CHECK_EQ(env.log,
            std::string("Key(2) Add(K2,77,c) Push(2) Blink(CleanerIcon) Key(400000) Add(K400000,77,c) Push(400000) Blink(CleanerIcon)"));
    OBL_CHECK_EQ(cleaner->m_BusyToilet, 0);
    OBL_CHECK_EQ(cleaner->m_cantDoJob, 0);
    OBL_CHECK_EQ(cleaner->m_FeedbackStatus, 4 | 2 | 0x400000);
    OBL_CHECK_EQ(cleaner->m_CurrentFeedback, 0x400000);

    // A thought without a text still reaches the member's own list and the button, but not the player.
    auto quiet = Member<Cleaner>();
    quiet->m_FeedbackStatus = 4 | 0x400000;
    quiet->m_BusyToilet = 1;
    FeedbackEnv none;
    none.noKey.insert(2);
    staff::UpdateCleanerFeedback(*quiet, none);
    OBL_CHECK_EQ(none.log, std::string("Key(2) Push(2) Blink(CleanerIcon)"));
}

OBL_TEST_CASE("staff_feedback.mechanic_repairs_waiting_once_then_only_a_thought_that_keeps_the_gate_shut")
{
    auto mechanic = Member<Mechanic>();
    mechanic->m_FeedbackStatus = 0x400000;
    FeedbackEnv none;
    staff::UpdateMechanicFeedback(*mechanic, none); // nothing to repair
    OBL_CHECK_EQ(none.log, std::string("Repair"));
    OBL_CHECK_EQ(mechanic->m_FeedbackStatus, 2 | 0x400000);

    auto first = Member<Mechanic>();
    first->m_FeedbackStatus = 0x400000;
    FeedbackEnv env;
    env.urgentJobs = 3;
    staff::UpdateMechanicFeedback(*first, env);
    OBL_CHECK_EQ(env.log, std::string("Repair Get(8) Mark(2,8) Key(2) Add(K2,77,9) Push(2) Blink(MechanicIcon)"));
    OBL_CHECK_EQ(first->m_RequiredFeedback, 0);

    auto second = Member<Mechanic>();
    second->m_FeedbackStatus = 0x400000;
    FeedbackEnv told;
    told.urgentJobs = 1;
    told.messages[8] = 2;
    staff::UpdateMechanicFeedback(*second, told);
    OBL_CHECK_EQ(told.log, std::string("Repair Get(8) Push(2)"));
    OBL_CHECK_EQ(second->m_RequiredFeedback, 2); // the gate stays shut until a script clears it
}

OBL_TEST_CASE("staff_feedback.mechanic_requirement_left_by_the_thought_reaches_the_next_blocks")
{
    auto mechanic = Member<Mechanic>(0.1f);
    mechanic->m_cantDoJob = 1;
    FeedbackEnv env;
    env.urgentJobs = 1;
    env.messages[8] = 2;
    staff::UpdateMechanicFeedback(*mechanic, env);
    // The requirement 2 stays set, so the "can't do the job" text carries it and the unhappy block ORs its own bit into what is left (0).
    OBL_CHECK_EQ(env.log,
            std::string("Repair Get(8) Push(2) Key(400002) Add(K400002,77,9) Push(400002) Blink(MechanicIcon) Key(100000) "
                        "Add(K100000,77,7) Push(800000)"));
    OBL_CHECK_EQ(mechanic->m_RequiredFeedback, 0x800000);
    OBL_CHECK_EQ(mechanic->m_cantDoJob, 0);
}

OBL_TEST_CASE("staff_feedback.lifeguard_drowned_then_called_out_then_three_job_thoughts")
{
    auto lifeguard = Member<Lifeguard>();
    lifeguard->m_GuestDrowned = 1;
    lifeguard->m_timesCalledOut = 9;
    lifeguard->m_cantDoJob = 1;
    FeedbackEnv env;
    staff::UpdateLifeguardFeedback(*lifeguard, env);
    OBL_CHECK_EQ(env.log,
            std::string("Mark(2,7) Key(2) Add(K2,77,8) Push(2) Blink(LifeguardIcon) Get(7) Mark(4,7) Key(4) Add(K4,77,8) Push(4) "
                        "Blink(LifeguardIcon) "
                        "Key(8) Add(K8,77,8) Push(8) Blink(LifeguardIcon) Key(10) Add(K10,77,8) Push(10) Blink(LifeguardIcon) "
                        "Key(20) Add(K20,77,8) Push(20) Blink(LifeguardIcon)"));
}

OBL_TEST_CASE("staff_feedback.lifeguard_flags")
{
    auto lifeguard = Member<Lifeguard>();
    lifeguard->m_GuestDrowned = 1;
    lifeguard->m_timesCalledOut = 8; // more than eight is needed (unsigned)
    lifeguard->m_cantDoJob = 1;
    FeedbackEnv env;
    staff::UpdateLifeguardFeedback(*lifeguard, env);
    OBL_CHECK(env.log.find("Mark(4,7)") == std::string::npos);
    OBL_CHECK_EQ(lifeguard->m_GuestDrowned, 0);
    OBL_CHECK_EQ(lifeguard->m_cantDoJob, 1); // the three job thoughts leave the flag alone
    OBL_CHECK_EQ(lifeguard->m_FeedbackStatus, 2 | 4 | 8 | 0x10 | 0x20);

    auto negative = Member<Lifeguard>();
    negative->m_FeedbackStatus = 2 | 8 | 0x10 | 0x20;
    negative->m_timesCalledOut = -1; // 0xffffffff, unsigned
    FeedbackEnv big;
    staff::UpdateLifeguardFeedback(*negative, big);
    OBL_CHECK_EQ(big.log, std::string("Get(7) Mark(4,7) Key(4) Add(K4,77,8) Push(4) Blink(LifeguardIcon)"));

    // The drowned flag is only looked at while its status bit is clear; the flag survives when the bit was spent.
    auto spent = Member<Lifeguard>();
    spent->m_FeedbackStatus = 2 | 4 | 8 | 0x10 | 0x20;
    spent->m_GuestDrowned = 1;
    FeedbackEnv quiet;
    staff::UpdateLifeguardFeedback(*spent, quiet);
    OBL_CHECK_EQ(quiet.log, std::string());
    OBL_CHECK_EQ(spent->m_GuestDrowned, 1);
}

OBL_TEST_CASE("staff_feedback.rep_category_with_enough_reports_is_marked_and_its_counters_reset")
{
    for (uint32_t category = 0; category < 7; ++category) {
        auto rep = Member<Rep>();
        rep->m_FeedbackStatus = static_cast<int32_t>(category);
        rep->m_counters[category][0] = 4;
        rep->m_counters[category][1] = 2;
        rep->m_counters[category][2] = 9;
        rep->m_counters[category][3] = 8;
        rep->m_counters[category][4] = 7;
        FeedbackEnv env;
        staff::UpdateRepFeedback(*rep, env);
        const std::string mask = FeedbackEnv::hex(2u << category);
        OBL_CHECK_EQ(env.log, std::string("Get(9) Mark(" + mask + ",9) Key(" + mask + ") Add(K" + mask + ",77,7)"));
        OBL_CHECK_EQ(rep->m_counters[category][0], 0);
        OBL_CHECK_EQ(rep->m_counters[category][1], 0);
        OBL_CHECK_EQ(rep->m_counters[category][2], 9); // the other pair is not touched with this opinion
        OBL_CHECK_EQ(rep->m_counters[category][3], 8);
        OBL_CHECK_EQ(rep->m_counters[category][4], 7);
        OBL_CHECK_EQ(rep->m_RequiredFeedback, 1);
        OBL_CHECK_EQ(rep->m_FeedbackStatus, 0);
    }
}

OBL_TEST_CASE("staff_feedback.rep_with_a_high_opinion_sums_the_second_pair_and_leaves_one_counter")
{
    auto rep = Member<Rep>();
    rep->m_GuestOpinionValue = 2;
    rep->m_FeedbackStatus = 3;
    rep->m_counters[3][0] = 100;
    rep->m_counters[3][1] = 100;
    rep->m_counters[3][2] = 5;
    rep->m_counters[3][3] = 3;
    rep->m_counters[3][4] = 3;
    FeedbackEnv env;
    staff::UpdateRepFeedback(*rep, env);
    OBL_CHECK_EQ(env.log, std::string("Get(9) Mark(800,9) Key(800) Add(K800,77,7)"));
    OBL_CHECK_EQ(rep->m_counters[3][2], 0);
    OBL_CHECK_EQ(rep->m_counters[3][3], 0);
    OBL_CHECK_EQ(rep->m_counters[3][4], 3);
    OBL_CHECK_EQ(rep->m_counters[3][0], 100);
}

OBL_TEST_CASE("staff_feedback.rep_threshold_mask_and_unhappy")
{
    // Five reports are not enough; the gate is closed and the scan restarts at category 0; the already told bit skips the block.
    auto rep = Member<Rep>(0.2f);
    rep->m_FeedbackStatus = 1;
    rep->m_counters[1][0] = 3;
    rep->m_counters[1][1] = 2;
    FeedbackEnv env;
    staff::UpdateRepFeedback(*rep, env);
    OBL_CHECK_EQ(env.log, std::string("Get(9) Key(100000) Add(K100000,77,7) Push(800000)"));
    OBL_CHECK_EQ(rep->m_counters[1][0], 3);
    OBL_CHECK_EQ(rep->m_RequiredFeedback, 1 | 0x800000);
    OBL_CHECK_EQ(rep->m_FeedbackStatus, 0x800000);

    auto told = Member<Rep>();
    told->m_FeedbackStatus = 2;
    told->m_counters[2][0] = 50;
    FeedbackEnv messages;
    messages.messages[9] = 6; // the bit of category 1 and 2 (status 2 & 6)
    staff::UpdateRepFeedback(*told, messages);
    OBL_CHECK_EQ(messages.log, std::string("Get(9)"));
    OBL_CHECK_EQ(told->m_FeedbackStatus, 2);
    OBL_CHECK_EQ(told->m_RequiredFeedback, 0);
}

OBL_TEST_CASE("staff_feedback.rep_index_beyond_the_table_announces_nothing")
{
    auto rep = Member<Rep>();
    rep->m_FeedbackStatus = 8;
    for (auto& row : rep->m_counters)
        for (auto& counter : row) counter = 9;
    FeedbackEnv env;
    staff::UpdateRepFeedback(*rep, env);
    OBL_CHECK_EQ(env.log, std::string("Get(9)"));
    for (const auto& row : rep->m_counters)
        for (const auto counter : row) OBL_CHECK_EQ(counter, 9);
    OBL_CHECK_EQ(rep->m_RequiredFeedback, 1);
    OBL_CHECK_EQ(rep->m_FeedbackStatus, 0);
}

OBL_TEST_CASE("staff_feedback.security_guests_per_bin_vandals_and_fights")
{
    auto guard = Member<SecurityGuard>();
    guard->m_FeedbackStatus = 0x400000;
    FeedbackEnv env;
    env.guests = 31;
    env.named["bin"] = 2; // 2 * 15 = 30 < 31
    guard->m_numVandals = 6;
    guard->m_numFights = 6;
    staff::UpdateSecurityFeedback(*guard, env);
    OBL_CHECK_EQ(env.log,
            std::string("Get(c) Guests Count(bin) Mark(2,c) Key(2) Add(K2,77,d) Push(2) Blink(SecurityGuardIcon) "
                        "Get(c) Mark(4,c) Key(4) Add(K4,77,d) Push(4) Blink(SecurityGuardIcon) "
                        "Get(c) Mark(8,c) Key(8) Add(K8,77,d) Push(8) Blink(SecurityGuardIcon)"));
    OBL_CHECK_EQ(guard->m_FeedbackStatus, 0xe | 0x400000);

    auto equal = Member<SecurityGuard>();
    equal->m_FeedbackStatus = 0x400000;
    FeedbackEnv boundary;
    boundary.guests = 30;
    boundary.named["bin"] = 2; // not more guests than the limit
    equal->m_numVandals = 5;
    equal->m_numFights = -7;
    staff::UpdateSecurityFeedback(*equal, boundary);
    OBL_CHECK_EQ(boundary.log, std::string("Get(c) Guests Count(bin)"));
    OBL_CHECK_EQ(equal->m_FeedbackStatus, 2 | 0x400000);

    auto noBins = Member<SecurityGuard>();
    noBins->m_FeedbackStatus = 0x400000;
    FeedbackEnv zero;
    zero.guests = 1;
    staff::UpdateSecurityFeedback(*noBins, zero); // no litter bins at all: any guest is more than 15 per bin
    OBL_CHECK_EQ(zero.log, std::string("Get(c) Guests Count(bin) Mark(2,c) Key(2) Add(K2,77,d) Push(2) Blink(SecurityGuardIcon)"));
}

OBL_TEST_CASE("staff_feedback.security_announced_bits_are_spent_without_a_message")
{
    auto guard = Member<SecurityGuard>();
    guard->m_FeedbackStatus = 0x400000;
    guard->m_numVandals = 100;
    guard->m_numFights = 100;
    guard->m_cantDoJob = 1;
    FeedbackEnv env;
    env.messages[0xc] = 2 | 4 | 8;
    staff::UpdateSecurityFeedback(*guard, env);
    OBL_CHECK_EQ(env.log, std::string("Get(c) Get(c) Get(c)"));
    OBL_CHECK_EQ(guard->m_FeedbackStatus, 0xe | 0x400000);
    OBL_CHECK_EQ(guard->m_cantDoJob, 1); // its bit was set already, so the flag is not looked at
}
