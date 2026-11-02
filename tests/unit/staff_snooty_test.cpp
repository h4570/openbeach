#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/objects/staff_snooty_logic.h"
#include "object_storage.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using ui::SpriteInstance;
using enum obl::messages::MessageType;
namespace snooty = obl::staff::snooty;

namespace {

struct FakeEnv {
    std::string log;
    std::vector<Message> posted;
    std::vector<float> delays; // -1 for a message posted at once
    SpriteInstance sprite{};
    double length = 2.25;
    uint8_t movement = 0x33;

    void* CreateSprite(const char* name)
    {
        log += std::string("sprite(") + name + ") ";
        return &sprite;
    }
    void Stand(void* instance) { log += instance == &sprite ? "stand " : "stand-on-wrong-sprite "; }
    double PlayAnim(int32_t nameWord)
    {
        log += "play(" + std::to_string(nameWord) + ") ";
        return length;
    }
    void Post(const Message& message)
    {
        posted.push_back(message);
        delays.push_back(-1.0f);
    }
    void PostDelayed(const Message& message, float delay)
    {
        posted.push_back(message);
        delays.push_back(delay);
    }
    uint8_t Movement(const Message&)
    {
        log += "movement ";
        return movement;
    }
};

struct Member {
    obl::object::SnootyStaff object;
    explicit Member(FakeEnv& env)
    {
        obl::unit::FillObject(object);
        object.m_id = 41;
        object.m_HomeID = 9;
        object.m_sprite = &env.sprite;
    }
};

OBL_TEST_CASE("staff_snooty.construct_builds_the_staff_member_and_stands_the_sprite")
{
    FakeEnv env;
    Member member(env);
    member.object.m_sprite = nullptr;
    snooty::Construct(member.object, "SNOB", env);
    OBL_CHECK_EQ(env.log, std::string("sprite(SNOB) stand "));
    OBL_CHECK(member.object.m_sprite == &env.sprite);
    OBL_CHECK_EQ(member.object.m_maxForce, 20.0f);
    OBL_CHECK_EQ(member.object.m_maxVel, 60.0f);
}

OBL_TEST_CASE("staff_snooty.im_here_is_passed_on_to_home")
{
    FakeEnv env;
    Member member(env);
    Message told{};
    told.type = MT_MOVEIMHERE;
    told.data = 5;
    OBL_CHECK_EQ(snooty::ReceiveMessage(member.object, told, env), staff::service::LowByte(told));
    OBL_CHECK_EQ(env.posted.size(), size_t(1));
    OBL_CHECK_EQ(env.delays[0], -1.0f);
    OBL_CHECK_EQ(env.posted[0].sender, 41);
    OBL_CHECK_EQ(env.posted[0].receiver, 9);
    OBL_CHECK_EQ(env.posted[0].type, int(MT_MOVEIMHERE));
    OBL_CHECK_EQ(env.posted[0].flags, 0);
    OBL_CHECK_EQ(env.posted[0].data, 0);
}

OBL_TEST_CASE("staff_snooty.an_animation_plays_once_and_home_hears_when_it_ends")
{
    FakeEnv env;
    Member member(env);
    Message told{};
    told.type = MT_PLAYANIMATION;
    told.data = 1234;
    OBL_CHECK_EQ(snooty::ReceiveMessage(member.object, told, env), staff::service::LowByte(2.25f));
    OBL_CHECK_EQ(env.log, std::string("play(1234) "));
    OBL_CHECK_EQ(env.sprite.m_animType, 2);
    OBL_CHECK_EQ(env.posted.size(), size_t(1));
    OBL_CHECK_EQ(env.delays[0], 2.25f);
    OBL_CHECK_EQ(env.posted[0].sender, 41);
    OBL_CHECK_EQ(env.posted[0].receiver, 9);
    OBL_CHECK_EQ(env.posted[0].type, int(MT_GUEST_SERVED));
    OBL_CHECK_EQ(env.posted[0].data, 0);

    FakeEnv quiet;
    Member silent(quiet);
    told.flags = 7;
    OBL_CHECK_EQ(snooty::ReceiveMessage(silent.object, told, quiet), staff::service::LowByte(2.25f));
    OBL_CHECK(quiet.posted.empty());
    OBL_CHECK_EQ(quiet.sprite.m_animType, 2);
}

OBL_TEST_CASE("staff_snooty.other_messages_go_to_the_movement_and_states_are_always_entered")
{
    FakeEnv env;
    Member member(env);
    Message told{};
    told.type = MT_NEW_DAY;
    OBL_CHECK_EQ(snooty::ReceiveMessage(member.object, told, env), uint8_t(0x33));
    OBL_CHECK_EQ(env.log, std::string("movement "));
    OBL_CHECK(snooty::OnEnterState(0));
    OBL_CHECK(snooty::OnEnterState(0xffffffffu));
}

OBL_TEST_CASE("staff_snooty.set_home_reports_ready_to_the_new_home")
{
    FakeEnv env;
    Member member(env);
    snooty::SetHome(member.object, 88, env);
    OBL_CHECK_EQ(member.object.m_HomeID, 88);
    OBL_CHECK_EQ(env.posted.size(), size_t(1));
    OBL_CHECK_EQ(env.posted[0].sender, 41);
    OBL_CHECK_EQ(env.posted[0].receiver, 88);
    OBL_CHECK_EQ(env.posted[0].type, int(MT_IM_READY));
    OBL_CHECK_EQ(env.delays[0], -1.0f);
}

} // namespace
