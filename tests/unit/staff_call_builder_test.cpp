#include "unit_test.h"

#include "game/objects/staff_call_builder.h"
#include "staff_call_fixture.h"

using namespace staff_call_test;
namespace call = obl::staff::call;

namespace {

struct Rig {
    std::unique_ptr<Builder> builder = Zeroed<Builder>();
    std::unique_ptr<obl::object::Base> target = Zeroed<obl::object::Base>();
    FakeEnv env;
    Rig()
    {
        builder->m_id = 12;
        builder->m_DestObjectID = 40;
        env.objects[40] = target.get();
    }
    void Run(int32_t id)
    {
        env.log.clear();
        call::BuilderCallFunction(*builder, id, env);
    }
};

OBL_TEST_CASE("staff_call_builder.visibility_time_and_forwarded_helpers")
{
    Rig rig;
    rig.Run(0xc);
    OBL_CHECK_EQ(rig.builder->m_visible, 1);
    rig.Run(0xb);
    OBL_CHECK_EQ(rig.builder->m_visible, 0);
    rig.Run(0x5c);
    OBL_CHECK_EQ(rig.builder->m_lastTaskTime, 12.5f);
    const struct {
        int32_t id;
        const char* log;
    } cases[] = {
            {0x5d, "tile "},
            {0x5e, ""},
            {0x5f, ""},
            {0x60, "females "},
            {0x62, "thought(20) "},
            {0x64, "costpoint(40) "},
            {0x65, "free(12) "},
            {0x66, "freenocheck(12) "},
            {0x67, "realloc "},
            {0x6a, "realloc "},
            {0xb2, "cleardelayed(12) "},
    };
    for (const auto& c : cases) {
        rig.Run(c.id);
        OBL_CHECK_EQ(rig.env.log, std::string(c.log));
    }
}

OBL_TEST_CASE("staff_call_builder.whistling_is_cleared")
{
    Rig rig;
    rig.Run(0x61);
    OBL_CHECK_EQ(rig.env.whistling, 0);
}

OBL_TEST_CASE("staff_call_builder.feedback_flags_and_death_wait")
{
    Rig rig;
    rig.builder->m_RequiredFeedback = 4;
    rig.builder->m_FeedbackStatus = 7;
    rig.Run(0x69);
    OBL_CHECK_EQ(rig.builder->m_RequiredFeedback, 0);
    OBL_CHECK_EQ(rig.builder->m_FeedbackStatus, 5);
    rig.Run(0xae);
    OBL_CHECK_EQ(rig.builder->m_waitingForDeath, 1);
}

OBL_TEST_CASE("staff_call_builder.the_delayed_message_waits_the_rest_of_the_hour")
{
    Rig rig;
    rig.builder->m_IdleTimePerHour = 3; // 240 / 24 - 3 = 7 left
    rig.env.rand = 0;
    rig.Run(0x68);
    OBL_CHECK_EQ(rig.env.log, std::string("object(40) delayed "));
    OBL_CHECK_EQ(rig.env.posted.sender, 12);
    OBL_CHECK_EQ(rig.env.posted.receiver, 12);
    OBL_CHECK_EQ(rig.env.posted.type, 0x39);
    OBL_CHECK_EQ(rig.env.posted.extra, 99);
    OBL_CHECK_EQ(rig.env.postedDelay, 7.0f);
    rig.env.demolish = true;
    rig.env.rand = 32767; // 7 * (1 - 32767 * 3.051851e-05) is about zero
    rig.Run(0x68);
    OBL_CHECK_EQ(rig.env.posted.extra, 100);
    OBL_CHECK(rig.env.postedDelay >= -0.001f && rig.env.postedDelay < 0.001f);
}

OBL_TEST_CASE("staff_call_builder.more_idle_time_than_an_hour_clamps_the_delay_to_zero")
{
    Rig rig;
    rig.builder->m_IdleTimePerHour = 50;
    rig.env.rand = 16384;
    rig.Run(0x68);
    OBL_CHECK_EQ(rig.env.postedDelay, 0.0f);
}

OBL_TEST_CASE("staff_call_builder.a_missing_destination_posts_nothing")
{
    Rig rig;
    rig.builder->m_DestObjectID = 99;
    rig.Run(0x68);
    OBL_CHECK_EQ(rig.env.log, std::string("object(99) "));
}

OBL_TEST_CASE("staff_call_builder.unrecognized_ids_go_to_the_shared_dispatcher")
{
    Rig rig;
    for (int32_t id : {0x63, 0xaf, 0xb0, 0xb1, 0x76, 0}) {
        rig.Run(id);
        OBL_CHECK_EQ(rig.env.log, "base(" + std::to_string(id) + ") ");
    }
}

} // namespace
