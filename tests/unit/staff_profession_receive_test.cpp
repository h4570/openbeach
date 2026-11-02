#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "unit_test.h"

#include "game/objects/staff_profession_receive.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::messages::Message;
using enum obl::messages::MessageType;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::Mechanic;
using obl::object::Rep;
using obl::object::SecurityGuard;

namespace {

// The game as a log: every call into it is written in order, the messages posted are kept.
struct Env {
    std::string log;
    float clock = 12.0f;
    bool base = true;
    std::vector<Message> posted;
    std::vector<int32_t> repairTypes{0, 0};
    size_t queried = 0;
    bool claimed = false;

    float GameDayTime() { return clock; }
    void ClearStaffMessages(int32_t type) { add("Clear(" + std::to_string(type) + ")"); }
    void Post(const Message& message)
    {
        posted.push_back(message);
        add("Post(" + std::to_string(message.receiver) + "," + std::to_string(message.type) + "," + std::to_string(message.data) + ")");
    }
    bool Base(const Message& message)
    {
        add("Base(" + std::to_string(message.type) + ")");
        return base;
    }
    void LeaveTower() { add("Leave"); }
    void ReleaseGuest(uint8_t sentToGuest) { add("Release(" + std::to_string(sentToGuest) + ")"); }
    void MoveToNextBuilding() { add("NextBuilding"); }
    void FreeBuilder(int32_t id) { add("Free(" + std::to_string(id) + ")"); }
    void SetState(int32_t state) { add("SetState(" + std::to_string(state) + ")"); }
    void EnterState(int32_t state) { add("Enter(" + std::to_string(state) + ")"); }
    void LookForOffender() { add("Offender"); }
    void DidNotClean(int32_t id) { add("NotClean(" + std::to_string(id) + ")"); }
    void CancelCleaning(int32_t id) { add("Cancel(" + std::to_string(id) + ")"); }
    int32_t RepairType(int32_t id)
    {
        add("Type(" + std::to_string(id) + ")");
        return repairTypes.at(queried++);
    }
    bool MechanicClaimed(int32_t id)
    {
        add("Claimed(" + std::to_string(id) + ")");
        return claimed;
    }
    void ReleaseRepair(int32_t id) { add("ReleaseRepair(" + std::to_string(id) + ")"); }
    void ReturnRepair(int32_t id, uint32_t type) { add("Return(" + std::to_string(id) + "," + std::to_string(type) + ")"); }
    void ReturnJobAndLeave(bool found) { add(found ? "LeaveRepair(1)" : "LeaveRepair(0)"); }
    void StopMechanicAnimation() { add("Direction"); }

    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

template<class T> std::unique_ptr<T> Member()
{
    auto member = std::make_unique<T>();
    obl::unit::FillObject<T>(*member);
    member->m_id = 77;
    member->m_HomeID = 42;
    member->m_happiness = 0.5f;
    return member;
}

Message Of(int type, int sender = 5, int data = 0)
{
    Message message{};
    message.type = type;
    message.sender = sender;
    message.receiver = 77;
    message.data = data;
    return message;
}

} // namespace

OBL_TEST_CASE("staff_profession_receive.the_hour_test_follows_the_c_remainder_idiom")
{
    using staff::profession_receive::EveryFourthHour;
    OBL_CHECK(EveryFourthHour(0.0f));
    OBL_CHECK(EveryFourthHour(4.0f));
    OBL_CHECK(EveryFourthHour(4.99f));
    OBL_CHECK(EveryFourthHour(20.0f));
    OBL_CHECK(!EveryFourthHour(5.0f));
    OBL_CHECK(!EveryFourthHour(7.5f));
    OBL_CHECK(EveryFourthHour(-4.0f)); // a negative multiple of four is a multiple: the idiom is a remainder, not a mask
    OBL_CHECK(EveryFourthHour(-8.5f));
    OBL_CHECK(!EveryFourthHour(-1.0f));
    OBL_CHECK(!EveryFourthHour(-3.0f));
    OBL_CHECK(EveryFourthHour(std::numeric_limits<float>::quiet_NaN())); // the float-to-int conversion gives 0
    OBL_CHECK(EveryFourthHour(std::numeric_limits<float>::infinity()));
    OBL_CHECK(EveryFourthHour(1e30f)); // beyond 64 bits: 0
    OBL_CHECK(EveryFourthHour(4294967296.0f)); // 2^32 wraps to 0
    OBL_CHECK(!EveryFourthHour(16777218.0f));
}

OBL_TEST_CASE("staff_profession_receive.unhappy_means_below_point_six_or_not_a_number")
{
    using staff::profession_receive::AsksNoMore;
    OBL_CHECK(AsksNoMore(8.0f, 0.5f));
    OBL_CHECK(AsksNoMore(8.0f, 0.0f));
    OBL_CHECK(AsksNoMore(8.0f, -1.0f));
    OBL_CHECK(!AsksNoMore(8.0f, 0.6f)); // the constant is the float 0.6f: equal is not below
    OBL_CHECK(!AsksNoMore(8.0f, 0.7f));
    OBL_CHECK(AsksNoMore(8.0f, std::numeric_limits<float>::quiet_NaN())); // an unordered pair compares as below
    OBL_CHECK(!AsksNoMore(8.0f, std::numeric_limits<float>::infinity()));
    OBL_CHECK(AsksNoMore(8.0f, -std::numeric_limits<float>::infinity()));
    OBL_CHECK(!AsksNoMore(9.0f, 0.1f)); // not an hour divisible by four
}

OBL_TEST_CASE("staff_profession_receive.new_day_zeroes_the_counters_of_each_profession_and_clears_its_messages")
{
    {
        auto member = Member<Lifeguard>();
        member->m_NumSaves = 5;
        member->m_NumDrowned = 3;
        member->m_timesCalledOut = 9;
        Env env;
        OBL_CHECK(staff::ReceiveLifeguard(*member, Of(MT_NEW_DAY), env));
        OBL_CHECK_EQ(member->m_NumSaves, 0);
        OBL_CHECK_EQ(member->m_NumDrowned, 0);
        OBL_CHECK_EQ(member->m_timesCalledOut, 9);
        OBL_CHECK_EQ(env.log, std::string("Clear(7) Base(38)"));
    }
    {
        auto member = Member<Rep>();
        member->m_happinessBoostToday = 4;
        member->m_GuestsTalkedToToday = 6;
        member->m_MinTalkTime = 2;
        Env env;
        staff::ReceiveRep(*member, Of(MT_NEW_DAY), env);
        OBL_CHECK_EQ(member->m_happinessBoostToday, 0);
        OBL_CHECK_EQ(member->m_GuestsTalkedToToday, 0);
        OBL_CHECK_EQ(member->m_MinTalkTime, 2);
        OBL_CHECK_EQ(env.log, std::string("Clear(9) Base(38)"));
    }
    {
        auto member = Member<SecurityGuard>();
        member->m_numMinorFinesToday = 1;
        member->m_numSeriousFinesToday = 2;
        member->m_numVandals = 3;
        member->m_numFights = 4;
        member->m_numJailed = 5;
        member->m_numCautioned = 6;
        member->m_fineTotalToday = 7.5f;
        member->m_minorFineAmount = 8.0f;
        Env env;
        staff::ReceiveSecurity(*member, Of(MT_NEW_DAY), env);
        OBL_CHECK_EQ(member->m_numMinorFinesToday, 0);
        OBL_CHECK_EQ(member->m_numSeriousFinesToday, 0);
        OBL_CHECK_EQ(member->m_numVandals, 0);
        OBL_CHECK_EQ(member->m_numFights, 0);
        OBL_CHECK_EQ(member->m_numJailed, 0);
        OBL_CHECK_EQ(member->m_numCautioned, 0);
        OBL_CHECK_EQ(member->m_fineTotalToday, 0.0f);
        OBL_CHECK_EQ(member->m_minorFineAmount, 8.0f);
        OBL_CHECK_EQ(env.log, std::string("Clear(12) Base(38)"));
    }
    {
        auto member = Member<Builder>();
        member->m_DestObjectID = 11;
        Env env;
        staff::ReceiveBuilder(*member, Of(MT_NEW_DAY), env);
        OBL_CHECK_EQ(member->m_DestObjectID, 11);
        OBL_CHECK_EQ(env.log, std::string("Clear(10) Base(38)"));
    }
}

OBL_TEST_CASE("staff_profession_receive.every_fourth_hour_an_unhappy_member_forgets_asking_for_feedback")
{
    auto member = Member<Lifeguard>();
    member->m_FeedbackStatus = 0x00c00003;
    member->m_RequiredFeedback = 5;
    Env env;
    env.clock = 8.5f;
    OBL_CHECK(staff::ReceiveLifeguard(*member, Of(MT_NEW_HOUR), env));
    OBL_CHECK_EQ(member->m_FeedbackStatus, 3 | 0x400000);
    OBL_CHECK_EQ(member->m_RequiredFeedback, 0);
    OBL_CHECK_EQ(env.log, std::string("Base(85)"));

    member->m_FeedbackStatus = 0x00c00003;
    member->m_RequiredFeedback = 5;
    env.clock = 9.0f; // not divisible by four
    staff::ReceiveLifeguard(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_FeedbackStatus, 0x00c00003);
    OBL_CHECK_EQ(member->m_RequiredFeedback, 5);

    env.clock = 8.0f;
    member->m_happiness = 0.9f; // happy
    staff::ReceiveLifeguard(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_FeedbackStatus, 0x00c00003);
    OBL_CHECK_EQ(member->m_RequiredFeedback, 5);
}

OBL_TEST_CASE("staff_profession_receive.the_hourly_message_clears_the_status_word_of_each_profession")
{
    Env env;
    env.clock = 0.0f;
    auto rep = Member<Rep>();
    rep->m_FeedbackStatus = static_cast<int32_t>(0xffffffffu);
    rep->m_RequiredFeedback = 1;
    staff::ReceiveRep(*rep, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(static_cast<uint32_t>(rep->m_FeedbackStatus), 0xff7fffffu);
    OBL_CHECK_EQ(rep->m_RequiredFeedback, 0);
    auto guard = Member<SecurityGuard>();
    guard->m_FeedbackStatus = 0x00800000;
    guard->m_RequiredFeedback = 1;
    staff::ReceiveSecurity(*guard, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(guard->m_FeedbackStatus, 0);
    OBL_CHECK_EQ(guard->m_RequiredFeedback, 0);
    auto builder = Member<Builder>();
    builder->m_FeedbackStatus = 0x00800001;
    builder->m_RequiredFeedback = 1;
    staff::ReceiveBuilder(*builder, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(builder->m_FeedbackStatus, 1);
    OBL_CHECK_EQ(builder->m_RequiredFeedback, 0);
}

OBL_TEST_CASE("staff_profession_receive.the_answer_is_the_answer_of_the_base")
{
    auto member = Member<Rep>();
    Env env;
    env.base = false;
    OBL_CHECK(!staff::ReceiveRep(*member, Of(1234), env));
    env.base = true;
    OBL_CHECK(staff::ReceiveRep(*member, Of(1234), env));
    OBL_CHECK_EQ(env.log, std::string("Base(1234) Base(1234)"));
}

OBL_TEST_CASE("staff_profession_receive.lifeguard_leaves_when_its_home_goes_and_stops_when_directed")
{
    auto member = Member<Lifeguard>();
    Env env;
    staff::ReceiveLifeguard(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
    OBL_CHECK_EQ(env.log, std::string("Leave Base(44)"));
    env.log.clear();
    staff::ReceiveLifeguard(*member, Of(MT_BUILDING_BEING_REMOVED, 43), env); // another building
    OBL_CHECK_EQ(env.log, std::string("Base(44)"));

    for (int state : {0x4e, 0x51}) {
        member->m_state = state;
        env.log.clear();
        env.posted.clear();
        staff::ReceiveLifeguard(*member, Of(MT_DIRECT_TO_POINT), env);
        OBL_CHECK_EQ(env.log, std::string("Post(77,21,0) Post(77,3,78) Base(86)"));
        OBL_CHECK_EQ(env.posted[0].sender, 77);
        OBL_CHECK_EQ(env.posted[1].sender, 77);
        OBL_CHECK_EQ(env.posted[1].flags, 0);
    }
    member->m_state = 0x4f;
    env.log.clear();
    staff::ReceiveLifeguard(*member, Of(MT_DIRECT_TO_POINT), env);
    OBL_CHECK_EQ(env.log, std::string("Base(86)"));
}

OBL_TEST_CASE("staff_profession_receive.rep_releases_its_guest_or_moves_on_when_the_building_it_walks_to_goes")
{
    auto member = Member<Rep>();
    member->m_state = 8;
    member->m_DestObjectID = 61;
    member->m_sentToGuest = 1;
    Env env;
    staff::ReceiveRep(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
    OBL_CHECK_EQ(env.log, std::string("Release(1) Base(44)"));

    env.log.clear();
    staff::ReceiveRep(*member, Of(MT_BUILDING_BEING_REMOVED, 61), env);
    OBL_CHECK_EQ(env.log, std::string("Post(77,21,0) NextBuilding Base(44)"));

    env.log.clear();
    member->m_state = 9; // not walking to a building
    staff::ReceiveRep(*member, Of(MT_BUILDING_BEING_REMOVED, 61), env);
    OBL_CHECK_EQ(env.log, std::string("Base(44)"));

    env.log.clear();
    member->m_state = 8;
    staff::ReceiveRep(*member, Of(MT_BUILDING_BEING_REMOVED, 62), env); // neither the home nor the target
    OBL_CHECK_EQ(env.log, std::string("Base(44)"));

    // the home wins when it is also the target
    member->m_DestObjectID = 42;
    env.log.clear();
    staff::ReceiveRep(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
    OBL_CHECK_EQ(env.log, std::string("Release(1) Base(44)"));
}

OBL_TEST_CASE("staff_profession_receive.rep_stops_when_directed_only_without_a_guest")
{
    auto member = Member<Rep>();
    member->m_state = 8;
    member->m_sentToGuest = 0;
    Env env;
    staff::ReceiveRep(*member, Of(MT_DIRECT_TO_POINT), env);
    OBL_CHECK_EQ(env.log, std::string("Post(77,21,0) Post(77,3,8) Base(86)"));
    env.log.clear();
    member->m_sentToGuest = 1;
    staff::ReceiveRep(*member, Of(MT_DIRECT_TO_POINT), env);
    OBL_CHECK_EQ(env.log, std::string("Base(86)"));
    env.log.clear();
    member->m_sentToGuest = 0;
    member->m_state = 7;
    staff::ReceiveRep(*member, Of(MT_DIRECT_TO_POINT), env);
    OBL_CHECK_EQ(env.log, std::string("Base(86)"));
}

OBL_TEST_CASE("staff_profession_receive.security_frees_its_offender_and_leaves_when_its_home_goes")
{
    auto member = Member<SecurityGuard>();
    member->m_offenderId = 900;
    Env env;
    for (int state : {0x7d, 0x7e, 0x7f}) {
        member->m_state = state;
        env.log.clear();
        env.posted.clear();
        staff::ReceiveSecurity(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
        OBL_CHECK_EQ(env.log, std::string("Post(900,3,12) SetState(76) Enter(76) Base(44)"));
        OBL_CHECK_EQ(env.posted[0].sender, 77);
        OBL_CHECK_EQ(env.posted[0].flags, 0);
    }
    // state 0x83 only while the guard can do its job; 0x80..0x82 and 0x7c never
    member->m_state = 0x83;
    member->m_cantDoJob = 0;
    env.log.clear();
    staff::ReceiveSecurity(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
    OBL_CHECK_EQ(env.log, std::string("Post(900,3,12) SetState(76) Enter(76) Base(44)"));
    member->m_cantDoJob = 1;
    env.log.clear();
    staff::ReceiveSecurity(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
    OBL_CHECK_EQ(env.log, std::string("SetState(76) Enter(76) Base(44)"));
    for (int state : {0x7c, 0x80, 0x82, 0x84, 0}) {
        member->m_state = state;
        member->m_cantDoJob = 0;
        env.log.clear();
        staff::ReceiveSecurity(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
        OBL_CHECK_EQ(env.log, std::string("SetState(76) Enter(76) Base(44)"));
    }
    // no offender: no message
    member->m_state = 0x7d;
    member->m_offenderId = 0;
    env.log.clear();
    staff::ReceiveSecurity(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
    OBL_CHECK_EQ(env.log, std::string("SetState(76) Enter(76) Base(44)"));
}

OBL_TEST_CASE("staff_profession_receive.security_looks_for_another_offender_when_its_target_goes")
{
    auto member = Member<SecurityGuard>();
    member->m_state = 0x78;
    member->m_DestObjectID = 61;
    Env env;
    staff::ReceiveSecurity(*member, Of(MT_BUILDING_BEING_REMOVED, 61), env);
    OBL_CHECK_EQ(env.log, std::string("Post(77,21,0) Offender Base(44)"));
    env.log.clear();
    member->m_state = 0x79;
    staff::ReceiveSecurity(*member, Of(MT_BUILDING_BEING_REMOVED, 61), env);
    OBL_CHECK_EQ(env.log, std::string("Base(44)"));
}

OBL_TEST_CASE("staff_profession_receive.security_stops_when_directed_on_patrol")
{
    auto member = Member<SecurityGuard>();
    member->m_state = 0x78;
    Env env;
    staff::ReceiveSecurity(*member, Of(MT_DIRECT_TO_POINT), env);
    OBL_CHECK_EQ(env.log, std::string("Post(77,21,0) Post(77,3,119) Base(86)"));
    env.log.clear();
    member->m_state = 0x77;
    staff::ReceiveSecurity(*member, Of(MT_DIRECT_TO_POINT), env);
    OBL_CHECK_EQ(env.log, std::string("Base(86)"));
}

OBL_TEST_CASE("staff_profession_receive.builder_takes_the_order_as_its_target_and_frees_its_job_when_its_home_goes")
{
    auto member = Member<Builder>();
    Env env;
    staff::ReceiveBuilder(*member, Of(MT_CONSTRUCT, 5, 301), env);
    OBL_CHECK_EQ(member->m_DestObjectID, 301);
    OBL_CHECK_EQ(env.log, std::string("Base(40)"));
    env.log.clear();
    staff::ReceiveBuilder(*member, Of(MT_DEMOLISH, 5, -1), env);
    OBL_CHECK_EQ(member->m_DestObjectID, -1);
    OBL_CHECK_EQ(env.log, std::string("Base(46)"));

    for (int state : {0x61, 0x62, 0x64, 0x67}) {
        member->m_state = state;
        env.log.clear();
        staff::ReceiveBuilder(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
        OBL_CHECK_EQ(env.log, std::string("Free(77) SetState(76) Enter(76) Base(44)"));
    }
    for (int state : {0x60, 0x65, 0x66, 0x68, 0, -1}) {
        member->m_state = state;
        env.log.clear();
        staff::ReceiveBuilder(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
        OBL_CHECK_EQ(env.log, std::string("SetState(76) Enter(76) Base(44)"));
    }
    env.log.clear();
    member->m_state = 0x61;
    staff::ReceiveBuilder(*member, Of(MT_BUILDING_BEING_REMOVED, 43), env); // not its home: nothing, not even a target check
    OBL_CHECK_EQ(env.log, std::string("Base(44)"));
}

OBL_TEST_CASE("staff_profession_receive.a_message_with_no_branch_only_reaches_the_base")
{
    Env env;
    auto lifeguard = Member<Lifeguard>();
    lifeguard->m_state = 0x4e;
    staff::ReceiveLifeguard(*lifeguard, Of(MT_DEMOLISH, 42), env); // builder messages mean nothing to a lifeguard
    staff::ReceiveLifeguard(*lifeguard, Of(MT_CONSTRUCT, 42), env);
    auto builder = Member<Builder>();
    builder->m_state = 0x4e;
    staff::ReceiveBuilder(*builder, Of(MT_DIRECT_TO_POINT, 42), env); // the builder has no DirectToPoint branch
    OBL_CHECK_EQ(env.log, std::string("Base(46) Base(40) Base(86)"));
    OBL_CHECK_EQ(lifeguard->m_DestObjectID, 0);
}

OBL_TEST_CASE("staff_profession_receive.cleaner_home_target_and_direct_orders_keep_distinct_effects")
{
    auto member = Member<Cleaner>();
    member->m_DestObjectID = 301;
    for (int state : {0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x83, -1}) {
        member->m_state = state;
        for (int cant : {0, 1, 255}) {
            member->m_cantDoJob = static_cast<uint8_t>(cant);
            Env env;
            staff::ReceiveCleaner(*member, Of(MT_BUILDING_BEING_REMOVED, 42), env);
            bool clean = (state >= 0x6d && state <= 0x73) || (state == 0x83 && cant == 0);
            OBL_CHECK_EQ(env.log, std::string(clean ? "NotClean(301) " : "") + "SetState(76) Enter(76) Base(44)");
        }
        Env env;
        staff::ReceiveCleaner(*member, Of(MT_BUILDING_BEING_REMOVED, 301), env);
        if (state == 0x6d || state == 0x6f || state == 0x70 || state == 0x83) {
            OBL_CHECK_EQ(env.log, std::string("Cancel(301) Post(77,21,0) SetState(104) Enter(104) Base(44)"));
        } else if (state == 0x6e || state == 0x71 || state == 0x73) {
            OBL_CHECK_EQ(env.log, std::string("Post(77,57,0) Base(44)"));
            OBL_CHECK_EQ(env.posted.at(0).extra, state);
            OBL_CHECK_EQ(env.posted.at(0).floatData, 0.0f);
        } else if (state == 0x72) {
            OBL_CHECK_EQ(env.log, std::string("Cancel(301) Post(77,107,0) Base(44)"));
        } else
            OBL_CHECK_EQ(env.log, std::string("Base(44)"));
        Env direct;
        staff::ReceiveCleaner(*member, Of(MT_DIRECT_TO_POINT), direct);
        bool stop = state == 0x6d || state == 0x6f || state == 0x70;
        OBL_CHECK_EQ(direct.log, std::string(stop ? "NotClean(301) Post(77,21,0) Post(77,3,104) " : "") + "Base(86)");
        Env unrelated;
        staff::ReceiveCleaner(*member, Of(MT_BUILDING_BEING_REMOVED, 302), unrelated);
        OBL_CHECK_EQ(unrelated.log, std::string("Base(44)"));
    }
    member->m_HomeID = member->m_DestObjectID;
    member->m_state = 0x6e;
    Env homeWins;
    staff::ReceiveCleaner(*member, Of(MT_BUILDING_BEING_REMOVED, 301), homeWins);
    OBL_CHECK_EQ(homeWins.log, std::string("NotClean(301) SetState(76) Enter(76) Base(44)"));
}

OBL_TEST_CASE("staff_profession_receive.mechanic_queries_short_circuit_and_can_return_different_types")
{
    auto member = Member<Mechanic>();
    member->m_DestObjectID = 301;
    member->m_state = 0x58;
    for (int first : {-1, 0, 1, 2, 3, 4})
        for (int second : {-1, 0, 1, 2, 3, 4})
            for (bool claimed : {false, true}) {
                Env env;
                env.repairTypes = {first, second};
                env.claimed = claimed;
                staff::ReceiveMechanic(*member, Of(MT_BUILDING_BEING_REMOVED, 301), env);
                std::string expected = "Type(301) ";
                if (first != 1) expected += "Type(301) ";
                if (first == 1 || second == 3)
                    expected += "ReleaseRepair(301) SetState(91) Enter(91) ";
                else
                    expected += claimed ? "Claimed(301) Post(77,107,0) " : "Claimed(301) ";
                OBL_CHECK_EQ(env.log, expected + "Base(44)");
                OBL_CHECK_EQ(env.queried, first == 1 ? size_t(1) : size_t(2));
            }
}

OBL_TEST_CASE("staff_profession_receive.mechanic_home_and_target_removal_depend_on_found_repair")
{
    auto member = Member<Mechanic>();
    member->m_DestObjectID = 301;
    for (int state : {0x57, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x83, -1})
        for (int found : {0, 1, 255}) {
            member->m_state = state;
            member->m_foundRepair = static_cast<uint8_t>(found);
            Env home;
            staff::ReceiveMechanic(*member, Of(MT_BUILDING_BEING_REMOVED, 42), home);
            OBL_CHECK_EQ(home.log, std::string(found ? "LeaveRepair(1) Base(44)" : "LeaveRepair(0) Base(44)"));
            Env target;
            staff::ReceiveMechanic(*member, Of(MT_BUILDING_BEING_REMOVED, 301), target);
            bool release = state == 0x5b || state == 0x5d || (state == 0x83 && found != 0);
            OBL_CHECK_EQ(target.log, std::string(release ? "ReleaseRepair(301) SetState(91) Enter(91) " : "") + "Base(44)");
        }
    member->m_HomeID = member->m_DestObjectID;
    member->m_state = 0x58;
    Env homeWins;
    staff::ReceiveMechanic(*member, Of(MT_BUILDING_BEING_REMOVED, 301), homeWins);
    OBL_CHECK_EQ(homeWins.log, std::string("LeaveRepair(1) Base(44)"));
}

OBL_TEST_CASE("staff_profession_receive.mechanic_direct_order_returns_only_known_types_then_stops_animation")
{
    auto member = Member<Mechanic>();
    member->m_DestObjectID = 301;
    for (int state : {0x58, 0x5b, 0x5c, 0x5d, 0x5e, 0x83})
        for (int type : {-1, 0, 1, 2, 3, 4}) {
            member->m_state = state;
            Env env;
            env.repairTypes = {type};
            env.base = false;
            OBL_CHECK(!staff::ReceiveMechanic(*member, Of(MT_DIRECT_TO_POINT), env));
            if (state != 0x5d)
                OBL_CHECK_EQ(env.log, std::string("Base(86)"));
            else {
                std::string expected = "Type(301) ";
                if (type >= 0 && type <= 3) expected += "Return(301," + std::to_string(type) + ") ";
                OBL_CHECK_EQ(env.log, expected + "Post(77,21,0) Direction Post(77,3,91) Base(86)");
            }
        }
    for (int dirs = 0; dirs <= 255; ++dirs)
        OBL_CHECK_EQ(staff::MechanicStoppedDirection(static_cast<uint8_t>(dirs)), dirs < 5 ? dirs - 1 : 4);
}

OBL_TEST_CASE("staff_profession_receive.cleaner_and_mechanic_reset_daily_counters_and_hourly_feedback")
{
    auto cleaner = Member<Cleaner>();
    cleaner->m_ToiletsToday = 3;
    cleaner->m_BinsToday = 4;
    cleaner->m_LitterToday = 5;
    cleaner->m_BusyToilet = 17;
    Env env;
    staff::ReceiveCleaner(*cleaner, Of(MT_NEW_DAY), env);
    OBL_CHECK_EQ(cleaner->m_ToiletsToday, 0);
    OBL_CHECK_EQ(cleaner->m_BinsToday, 0);
    OBL_CHECK_EQ(cleaner->m_LitterToday, 0);
    OBL_CHECK_EQ(cleaner->m_BusyToilet, 17);
    auto mechanic = Member<Mechanic>();
    mechanic->m_inspectionsToday = 3;
    mechanic->m_repairsToday = 4;
    mechanic->m_vandalsToday = 5;
    mechanic->m_wateredToday = 6;
    mechanic->m_repairType = 2;
    staff::ReceiveMechanic(*mechanic, Of(MT_NEW_DAY), env);
    OBL_CHECK_EQ(mechanic->m_inspectionsToday, 0);
    OBL_CHECK_EQ(mechanic->m_repairsToday, 0);
    OBL_CHECK_EQ(mechanic->m_vandalsToday, 0);
    OBL_CHECK_EQ(mechanic->m_wateredToday, 0);
    OBL_CHECK_EQ(mechanic->m_repairType, 2);
    OBL_CHECK_EQ(env.log, std::string("Clear(11) Base(38) Clear(8) Base(38)"));
    for (float hour : {8.0f, 9.0f, -4.0f})
        for (float happiness : {0.5f, 0.6f, std::numeric_limits<float>::quiet_NaN()}) {
            cleaner->m_happiness = mechanic->m_happiness = happiness;
            cleaner->m_FeedbackStatus = mechanic->m_FeedbackStatus = 0x00800021;
            cleaner->m_RequiredFeedback = mechanic->m_RequiredFeedback = 7;
            Env hourly;
            hourly.clock = hour;
            staff::ReceiveCleaner(*cleaner, Of(MT_NEW_HOUR), hourly);
            staff::ReceiveMechanic(*mechanic, Of(MT_NEW_HOUR), hourly);
            const bool forget = hour != 9.0f && !(happiness >= 0.6f);
            OBL_CHECK_EQ(cleaner->m_FeedbackStatus, forget ? 0x21 : 0x00800021);
            OBL_CHECK_EQ(mechanic->m_FeedbackStatus, cleaner->m_FeedbackStatus);
            OBL_CHECK_EQ(cleaner->m_RequiredFeedback, forget ? 0 : 7);
            OBL_CHECK_EQ(mechanic->m_RequiredFeedback, cleaner->m_RequiredFeedback);
        }
}
