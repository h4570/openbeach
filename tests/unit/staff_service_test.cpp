#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "unit_test.h"

#include "game/objects/staff_service_states.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using enum obl::messages::MessageType;
using namespace obl::tables;
namespace svc = obl::staff::service;

namespace {

struct FakeEnv {
    std::vector<Message> posted;
    std::vector<float> delays; // one per delayed message, in order; the message goes to `posted` too
    std::vector<bool> wasDelayed;
    bool humanOk = true;
    std::string log;

    void Post(const Message& message)
    {
        posted.push_back(message);
        wasDelayed.push_back(false);
    }
    void PostDelayed(const Message& message, float delay)
    {
        posted.push_back(message);
        wasDelayed.push_back(true);
        delays.push_back(delay);
    }
    bool HumanUpdate()
    {
        log += "Human ";
        return humanOk;
    }
    void Fatal(const char* name) { log += std::string("Fatal(") + name + ") "; }
};

std::unique_ptr<obl::object::ServiceStaff> Member(const char* name, const char* sprite, int id = 77, int home = 12)
{
    auto member = std::make_unique<obl::object::ServiceStaff>();
    obl::unit::FillObject<obl::object::ServiceStaff>(*member);
    member->m_id = id;
    member->m_HomeID = home;
    std::strcpy(member->m_Name, name);
    static StaffDef definition;
    definition.m_Sprite = sprite;
    member->m_staffDefinition = &definition;
    return member;
}

OBL_TEST_CASE("staff_service.has_state_answers_zero_one_and_the_dj_state")
{
    OBL_CHECK(svc::HasState(0));
    OBL_CHECK(svc::HasState(1));
    OBL_CHECK(svc::HasState(0x4b));
    OBL_CHECK(!svc::HasState(2));
    OBL_CHECK(!svc::HasState(0x4a));
    OBL_CHECK(!svc::HasState(0x4c));
    OBL_CHECK(!svc::HasState(0xffffffffu)); // unsigned compare: -1 is not "below 1"
}

OBL_TEST_CASE("staff_service.state_zero_sets_state_one_on_itself")
{
    FakeEnv env;
    auto member = Member("BAR_M", "BARMAN");
    OBL_CHECK(svc::EnterState(*member, 0, env));
    OBL_CHECK_EQ(env.posted.size(), size_t(1));
    OBL_CHECK_EQ(env.posted[0].sender, 77);
    OBL_CHECK_EQ(env.posted[0].receiver, 77);
    OBL_CHECK_EQ(env.posted[0].type, int(MT_STATECHANGE));
    OBL_CHECK_EQ(env.posted[0].flags, 0);
    OBL_CHECK_EQ(env.posted[0].data, 1);
}

OBL_TEST_CASE("staff_service.state_one_of_a_barman_tells_home_and_looks_around_later")
{
    FakeEnv env;
    auto member = Member("BAR_M", "BARMAN");
    OBL_CHECK(svc::EnterState(*member, 1, env));
    OBL_CHECK_EQ(env.posted.size(), size_t(3));
    OBL_CHECK_EQ(env.posted[0].type, int(MT_STATECHANGE));
    OBL_CHECK_EQ(env.posted[0].receiver, 77);
    OBL_CHECK_EQ(env.posted[0].data, 0x4b);
    OBL_CHECK_EQ(env.posted[1].type, int(MT_STAFF_CREATED));
    OBL_CHECK_EQ(env.posted[1].sender, 77);
    OBL_CHECK_EQ(env.posted[1].receiver, 12);
    OBL_CHECK_EQ(env.posted[1].data, 0);
    OBL_CHECK_EQ(env.posted[2].type, int(MT_LOOK_AROUND));
    OBL_CHECK_EQ(env.posted[2].receiver, 77);
    OBL_CHECK(env.wasDelayed[2] && !env.wasDelayed[0] && !env.wasDelayed[1]);
    OBL_CHECK_EQ(env.delays[0], 20.0f);
}

OBL_TEST_CASE("staff_service.the_dj_in_the_name_is_reported_to_home_and_other_sprites_do_not_look_around")
{
    FakeEnv env;
    auto member = Member("xDJx", "BARWOMAN");
    OBL_CHECK(svc::EnterState(*member, 1, env));
    OBL_CHECK_EQ(env.posted.size(), size_t(2));
    OBL_CHECK_EQ(env.posted[1].data, 1);
    FakeEnv lower;
    auto small = Member("dj", "BARWOMAN"); // the name is searched without case
    svc::EnterState(*small, 1, lower);
    OBL_CHECK_EQ(lower.posted[1].data, 1);
}

OBL_TEST_CASE("staff_service.other_states_do_nothing")
{
    FakeEnv env;
    auto member = Member("BAR_M", "BARMAN");
    for (uint32_t state : {2u, 0x4bu, 0x4cu, 0xffffffffu}) OBL_CHECK(svc::EnterState(*member, state, env));
    OBL_CHECK(env.posted.empty());
}

OBL_TEST_CASE("staff_service.update_ends_the_game_only_when_the_base_update_fails")
{
    FakeEnv ok;
    auto member = Member("BAR_M", "BARMAN");
    OBL_CHECK(svc::Update(*member, ok));
    OBL_CHECK_EQ(ok.log, std::string("Human "));
    FakeEnv bad;
    bad.humanOk = false;
    OBL_CHECK(svc::Update(*member, bad));
    OBL_CHECK_EQ(bad.log, std::string("Human Fatal(BAR_M) "));
}

} // namespace
