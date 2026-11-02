#include "unit_test.h"

#include "staff_call_fixture.h"
#include "game/objects/staff_call_security.h"

using namespace staff_call_test;
namespace call = obl::staff::call;
using obl::object::SecurityGuard;

namespace {

struct Rig {
    std::unique_ptr<SecurityGuard> security = Zeroed<SecurityGuard>();
    FakeEnv env;
    Rig() { security->m_id = 77; }
    void Run(int32_t id)
    {
        env.log.clear();
        call::SecurityCallFunction(*security, id, env);
    }
};

OBL_TEST_CASE("staff_call_security.visibility_and_thought")
{
    Rig rig;
    rig.Run(0xc);
    OBL_CHECK_EQ(rig.security->m_visible, 1);
    rig.Run(0xb);
    OBL_CHECK_EQ(rig.security->m_visible, 0);
    rig.Run(0x7b);
    OBL_CHECK_EQ(rig.env.log, std::string("thought(20000000) "));
}

OBL_TEST_CASE("staff_call_security.the_offender_search_result_is_stored")
{
    Rig rig;
    rig.env.offender = true;
    rig.Run(0x7e);
    OBL_CHECK_EQ(rig.security->m_misdemeanourFound, 1);
    rig.env.offender = false;
    rig.Run(0x7e);
    OBL_CHECK_EQ(rig.security->m_misdemeanourFound, 0);
}

OBL_TEST_CASE("staff_call_security.fines_accumulate_per_kind")
{
    Rig rig;
    rig.security->m_seriousFineAmount = 50;
    rig.security->m_minorFineAmount = 5;
    rig.Run(0x7f);
    OBL_CHECK_EQ(rig.env.log, std::string("fine(SERIOUS) "));
    rig.Run(0x80);
    rig.Run(0x80);
    OBL_CHECK_EQ(rig.security->m_numSeriousFinesToday, 1);
    OBL_CHECK_EQ(rig.security->m_numMinorFinesToday, 2);
    OBL_CHECK_EQ(rig.security->m_fineTotalToday, 60.0f);
}

OBL_TEST_CASE("staff_call_security.jailed_and_cautioned_counters")
{
    Rig rig;
    rig.Run(0x81);
    rig.Run(0x82);
    rig.Run(0x82);
    OBL_CHECK_EQ(rig.security->m_numJailed, 1);
    OBL_CHECK_EQ(rig.security->m_numCautioned, 2);
}

OBL_TEST_CASE("staff_call_security.the_fine_position_is_offset_from_the_serve_point")
{
    Rig rig;
    rig.security->m_HomeID = 9;
    rig.env.servePoint[0] = 100;
    rig.env.servePoint[1] = 40;
    rig.Run(0x83);
    OBL_CHECK_EQ(rig.env.log, std::string("serve(9) "));
    OBL_CHECK_EQ(rig.security->m_FineOffsetX, 122.0f);
    OBL_CHECK_EQ(rig.security->m_FineOffsetY, 40.0f);
}

OBL_TEST_CASE("staff_call_security.offender_helpers_and_detention_feedback")
{
    Rig rig;
    rig.security->m_offenderId = 31;
    rig.Run(0x85);
    OBL_CHECK_EQ(rig.env.log, std::string("face(31) "));
    rig.Run(0x86);
    OBL_CHECK_EQ(rig.env.log, std::string("choose "));
    rig.Run(0x87);
    OBL_CHECK_EQ(rig.env.log, std::string("feedback(SECURITY_GUARD_HAS_DETAINED,77,8) "));
}

OBL_TEST_CASE("staff_call_security.select_and_feedback_use_the_guard_icon_names")
{
    Rig rig;
    rig.Run(0xaf);
    OBL_CHECK_EQ(rig.env.log, std::string("blink(SecurityIcon,1) "));
    rig.Run(0xae);
    OBL_CHECK_EQ(rig.security->m_waitingForDeath, 1);
    rig.security->m_RequiredFeedback = 3;
    rig.Run(0xb0);
    OBL_CHECK_EQ(rig.security->m_CurrentFeedback, 3);
    OBL_CHECK_EQ(rig.env.log, std::string("select tab(SecurityDialog,2) thought(3) blink(SecurityGuardIcon,0) "));
    rig.security->m_RequiredFeedback = 4;
    rig.env.feedbackKey = "K";
    rig.Run(0xb1);
    OBL_CHECK_EQ(rig.env.log, std::string("key(4) feedback(K,77,13) thought(4) blink(SecurityGuardIcon,0) "));
    OBL_CHECK_EQ(rig.security->m_RequiredFeedback, 0);
}

OBL_TEST_CASE("staff_call_security.unrecognized_ids_go_to_the_shared_dispatcher")
{
    Rig rig;
    for (int32_t id : {0x84, 0xad, 0x76, 0}) {
        rig.Run(id);
        OBL_CHECK_EQ(rig.env.log, "base(" + std::to_string(id) + ") ");
    }
}

} // namespace
