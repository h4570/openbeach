#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "unit_test.h"

#include "game/objects/staff_state.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::messages::Message;
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
    std::vector<Message> posted;
    bool homeExists = true;
    int32_t area = 0x1234;

    void BaseSack() { add("Sack"); }
    void FreeBuilder(int32_t id) { add("Free(" + std::to_string(id) + ")"); }
    void DidNotClean(int32_t id) { add("NotClean(" + std::to_string(id) + ")"); }
    void LeaveTower() { add("Leave"); }
    void ReturnJobAndLeave(bool found) { add(found ? "LeaveRepair(1)" : "LeaveRepair(0)"); }
    void ReleaseGuest(uint8_t sent) { add("Release(" + std::to_string(sent) + ")"); }
    void Post(const Message& message)
    {
        posted.push_back(message);
        add("Post(" + std::to_string(message.sender) + ">" + std::to_string(message.receiver) + "," + std::to_string(message.type) + "," +
                std::to_string(message.flags) + "," + std::to_string(message.data) + ")");
    }
    void SetState(int32_t state) { add("SetState(" + std::to_string(state) + ")"); }
    void EnterState(int32_t state) { add("Enter(" + std::to_string(state) + ")"); }
    void AddBuilder(int32_t id) { add("AddBuilder(" + std::to_string(id) + ")"); }
    bool HomeInfluenceArea(int32_t& out)
    {
        add("Home");
        if (homeExists) out = area;
        return homeExists;
    }
    void RunEntryScript(uint32_t state) { add("Entry(" + std::to_string(state) + ")"); }
    void RunExitScript(int32_t state) { add("Exit(" + std::to_string(state) + ")"); }

    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

template<class T> std::unique_ptr<T> Member()
{
    auto member = std::make_unique<T>();
    obl::unit::FillObject<T>(*member);
    member->m_id = 77;
    member->m_HomeID = 42;
    member->m_DestObjectID = 301;
    return member;
}

} // namespace

OBL_TEST_CASE("staff_state.builder_sack_frees_the_job_in_the_job_states_only")
{
    for (int state = 0; state < 0x90; ++state) {
        auto member = Member<Builder>();
        member->m_state = state;
        Env env;
        staff::SackBuilder(*member, env);
        const bool job = (state > 0x60 && state < 0x65) || state == 0x67;
        OBL_CHECK_EQ(env.log, std::string("Sack ") + (job ? "Free(77) " : "") + "SetState(76) Enter(76)");
    }
}

OBL_TEST_CASE("staff_state.builder_state_is_read_as_unsigned")
{
    auto member = Member<Builder>();
    member->m_state = -1;
    Env env;
    staff::SackBuilder(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Sack SetState(76) Enter(76)")); // 0xffffffff is not a job state (the test is `cmp` / `ja`, unsigned)
}

OBL_TEST_CASE("staff_state.cleaner_sack_gives_the_job_back_in_states_6d_to_73_and_83_when_able")
{
    for (int state = 0; state < 0x90; ++state) {
        for (int cant = 0; cant < 2; ++cant) {
            auto member = Member<Cleaner>();
            member->m_state = state;
            member->m_cantDoJob = static_cast<uint8_t>(cant ? 5 : 0);
            Env env;
            staff::SackCleaner(*member, env);
            const bool job = (state >= 0x6d && state <= 0x73) || (state == 0x83 && !cant);
            OBL_CHECK_EQ(env.log, std::string("Sack ") + (job ? "NotClean(301) " : "") + "SetState(76) Enter(76)");
        }
    }
}

OBL_TEST_CASE("staff_state.lifeguard_mechanic_and_rep_sack_call_their_own_leaving")
{
    {
        auto member = Member<Lifeguard>();
        Env env;
        staff::SackLifeguard(*member, env);
        OBL_CHECK_EQ(env.log, std::string("Sack Leave"));
    }
    {
        auto member = Member<Mechanic>();
        Env env;
        staff::SackMechanic(*member, env);
        OBL_CHECK_EQ(env.log, std::string("Sack LeaveRepair(0)"));
        member->m_foundRepair = 7;
        Env again;
        staff::SackMechanic(*member, again);
        OBL_CHECK_EQ(again.log, std::string("Sack LeaveRepair(1)"));
    }
    {
        auto member = Member<Rep>();
        member->m_sentToGuest = 9;
        Env env;
        staff::SackRep(*member, env);
        OBL_CHECK_EQ(env.log, std::string("Sack Release(9)"));
    }
}

OBL_TEST_CASE("staff_state.security_sack_releases_the_offender_in_the_offence_states")
{
    for (int state = 0; state < 0x90; ++state) {
        for (int cant = 0; cant < 2; ++cant) {
            for (int offender = 0; offender < 2; ++offender) {
                auto member = Member<SecurityGuard>();
                member->m_state = state;
                member->m_cantDoJob = static_cast<uint8_t>(cant);
                member->m_offenderId = offender ? 555 : 0;
                Env env;
                staff::SackSecurity(*member, env);
                const bool dealing = (state >= 0x7d && state <= 0x7f) || (state == 0x83 && !cant);
                OBL_CHECK_EQ(
                        env.log, std::string("Sack ") + (dealing && offender ? "Post(77>555,3,0,12) " : "") + "SetState(76) Enter(76)");
            }
        }
    }
}

OBL_TEST_CASE("staff_state.state_one_tells_the_home_and_the_lifeguard_copies_the_influence_area")
{
    {
        auto member = Member<Builder>();
        Env env;
        OBL_CHECK(staff::EnterBuilder(*member, 1, env));
        OBL_CHECK_EQ(env.log, std::string("Post(77>42,37,0,0) AddBuilder(77) Entry(1)"));
    }
    {
        auto member = Member<Cleaner>();
        Env env;
        OBL_CHECK(staff::EnterCleaner(*member, 1, env));
        OBL_CHECK_EQ(env.log, std::string("Post(77>42,37,0,0) Entry(1)"));
    }
    {
        auto member = Member<Mechanic>();
        Env env;
        OBL_CHECK(staff::EnterMechanic(*member, 1, env));
        OBL_CHECK_EQ(env.log, std::string("Post(77>42,37,0,0) Entry(1)"));
    }
    {
        auto member = Member<Rep>();
        Env env;
        OBL_CHECK(staff::EnterRepOrSecurity(*member, 1, env));
        OBL_CHECK_EQ(env.log, std::string("Post(77>42,37,0,0) Entry(1)"));
    }
    {
        auto member = Member<SecurityGuard>();
        Env env;
        OBL_CHECK(staff::EnterRepOrSecurity(*member, 1, env));
        OBL_CHECK_EQ(env.log, std::string("Post(77>42,37,0,0) Entry(1)"));
    }
    {
        auto member = Member<Lifeguard>();
        Env env;
        OBL_CHECK(staff::EnterLifeguard(*member, 1, env));
        OBL_CHECK_EQ(env.log, std::string("Post(77>42,37,0,0) Home Entry(1)"));
        OBL_CHECK_EQ(member->m_InfluenceArea, 0x1234);
    }
}

OBL_TEST_CASE("staff_state.a_lifeguard_without_a_home_keeps_its_area")
{
    auto member = Member<Lifeguard>();
    member->m_InfluenceArea = 99;
    Env env;
    env.homeExists = false;
    OBL_CHECK(staff::EnterLifeguard(*member, 1, env));
    OBL_CHECK_EQ(member->m_InfluenceArea, 99);
}

OBL_TEST_CASE("staff_state.home_target_states_copy_the_home_into_the_target")
{
    {
        auto member = Member<Builder>();
        Env env;
        OBL_CHECK(staff::EnterBuilder(*member, 0x60, env));
        OBL_CHECK_EQ(member->m_DestObjectID, 42);
        OBL_CHECK_EQ(env.log, std::string("Entry(96)"));
    }
    {
        auto member = Member<Cleaner>();
        Env env;
        OBL_CHECK(staff::EnterCleaner(*member, 0x69, env));
        OBL_CHECK_EQ(member->m_DestObjectID, 42);
    }
    {
        auto member = Member<Mechanic>();
        Env env;
        OBL_CHECK(staff::EnterMechanic(*member, 0x5c, env));
        OBL_CHECK_EQ(member->m_DestObjectID, 42);
    }
    {
        // Another profession's state does not touch the target, and no message is sent for any state but 1.
        auto member = Member<Cleaner>();
        Env env;
        OBL_CHECK(staff::EnterCleaner(*member, 0x5c, env));
        OBL_CHECK_EQ(member->m_DestObjectID, 301);
        OBL_CHECK(env.posted.empty());
    }
}

OBL_TEST_CASE("staff_state.exit_runs_the_script_of_the_current_state")
{
    auto member = Member<Cleaner>();
    member->m_state = 0x6e;
    Env env;
    OBL_CHECK(staff::ExitStaff(*member, env));
    OBL_CHECK_EQ(env.log, std::string("Exit(110)"));
}
