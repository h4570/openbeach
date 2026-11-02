#include "unit_test.h"
#include "game/messages/message_state.h"

#include "staff_call_fixture.h"
#include "game/objects/staff_call_rep.h"

using namespace staff_call_test;
namespace call = obl::staff::call;
using obl::object::Rep;

namespace {

struct Rig {
    std::unique_ptr<Rep> rep = Zeroed<Rep>();
    Message message{};
    FakeEnv env;
    Rig()
    {
        rep->m_id = 55;
        rep->m_currentMessage = &message;
    }
    void Run(int32_t id)
    {
        env.log.clear();
        call::RepCallFunction(*rep, id, env);
    }
};

OBL_TEST_CASE("staff_call_rep.forwarded_helpers_and_visibility")
{
    Rig rig;
    rig.Run(0xc);
    OBL_CHECK_EQ(rig.rep->m_visible, 1);
    rig.Run(0xb);
    OBL_CHECK_EQ(rig.rep->m_visible, 0);
    const struct {
        int32_t id;
        const char* log;
    } cases[] = {
            {0x7b, "thought(20000000) "},
            {0xa2, "nextbuilding "},
            {0xa3, "lookguests "},
            {0xa4, "arrived "},
            {0xa6, "thought(20000) "},
    };
    for (const auto& c : cases) {
        rig.Run(c.id);
        OBL_CHECK_EQ(rig.env.log, std::string(c.log));
    }
}

OBL_TEST_CASE("staff_call_rep.talk_time_is_scaled_and_accumulated_in_a_float")
{
    Rig rig;
    rig.rep->m_timeWithGuests = 5.0f; // 5 * 0.8f * 10 = 40.000001 -> 40
    rig.Run(0xa5);
    OBL_CHECK_EQ(rig.rep->m_GuestsTalkedToToday, 1);
    OBL_CHECK_EQ(std::bit_cast<float>(rig.rep->m_happinessBoostToday), 40.0f);
    rig.rep->m_timeWithGuests = 0.0f; // at least one unit
    rig.Run(0xa5);
    OBL_CHECK_EQ(rig.rep->m_GuestsTalkedToToday, 2);
    OBL_CHECK_EQ(std::bit_cast<float>(rig.rep->m_happinessBoostToday), 41.0f);
    rig.rep->m_timeWithGuests = -3.0f;
    rig.Run(0xa5);
    OBL_CHECK_EQ(std::bit_cast<float>(rig.rep->m_happinessBoostToday), 42.0f);
}

OBL_TEST_CASE("staff_call_rep.an_opinion_message_updates_the_counters_and_feedback")
{
    Rig rig;
    rig.message.data = 3;
    rig.message.flags = 1;
    rig.rep->m_RequiredFeedback = 9;
    rig.Run(0xa7);
    OBL_CHECK_EQ(rig.rep->m_counters[3][1], 1);
    OBL_CHECK_EQ(rig.rep->m_RequiredFeedback, 0);
    OBL_CHECK_EQ(rig.rep->m_FeedbackStatus, 3);
    OBL_CHECK_EQ(rig.rep->m_GuestOpinionValue, 1);
    OBL_CHECK_EQ(rig.env.log, std::string("updatefeedback "));
}

OBL_TEST_CASE("staff_call_rep.a_neutral_opinion_only_counts")
{
    Rig rig;
    rig.message.data = 6;
    rig.message.flags = 2;
    rig.rep->m_RequiredFeedback = 9;
    rig.Run(0xa7);
    rig.Run(0xa7);
    OBL_CHECK_EQ(rig.rep->m_counters[6][2], 2);
    OBL_CHECK_EQ(rig.rep->m_RequiredFeedback, 9);
    OBL_CHECK_EQ(rig.env.log, std::string());
}

OBL_TEST_CASE("staff_call_rep.select_and_feedback")
{
    Rig rig;
    rig.Run(0xaf);
    OBL_CHECK_EQ(rig.env.log, std::string("blink(RepIcon,1) "));
    rig.Run(0xae);
    OBL_CHECK_EQ(rig.rep->m_waitingForDeath, 1);
    rig.rep->m_RequiredFeedback = 2;
    rig.Run(0xb0);
    OBL_CHECK_EQ(rig.env.log, std::string("select tab(RepDialog,2) thought(2) blink(RepIcon,0) "));
    rig.rep->m_RequiredFeedback = 8;
    rig.env.feedbackKey = "REP";
    rig.Run(0xb1);
    OBL_CHECK_EQ(rig.env.log, std::string("key(8) feedback(REP,55,10) thought(8) blink(RepIcon,0) "));
    OBL_CHECK_EQ(rig.rep->m_CurrentFeedback, 8);
}

OBL_TEST_CASE("staff_call_rep.unrecognized_ids_go_to_the_shared_dispatcher")
{
    Rig rig;
    for (int32_t id : {0xad, 0x76, 0xa8, 0}) {
        rig.Run(id);
        OBL_CHECK_EQ(rig.env.log, "base(" + std::to_string(id) + ") ");
    }
}

} // namespace
