#include "unit_test.h"

#include "staff_call_fixture.h"
#include "game/objects/staff_call_lifeguard.h"

using namespace staff_call_test;
namespace call = obl::staff::call;

namespace {

struct Rig {
    std::unique_ptr<Lifeguard> lifeguard = Zeroed<Lifeguard>();
    std::unique_ptr<Lifeguard> guest = Zeroed<Lifeguard>();
    std::unique_ptr<Lifeguard> home = Zeroed<Lifeguard>();
    SpriteRig selfSprite, guestSprite, homeSprite;
    FakeEnv env;

    Rig(int32_t selfDir = 0, int32_t guestDir = 0, int32_t homeDir = 0)
        : selfSprite(*lifeguard, 8, selfDir), guestSprite(*guest, 8, guestDir), homeSprite(*home, 8, homeDir)
    {
        lifeguard->m_id = 100;
        lifeguard->m_saving_id = 200;
        lifeguard->m_HomeID = 300;
        env.objects[200] = guest.get();
        env.objects[300] = home.get();
        env.hotel = home.get();
        home->m_id = 300;
    }
    void Run(int32_t id)
    {
        env.log.clear();
        call::LifeguardCallFunction(*lifeguard, id, env);
    }
};

OBL_TEST_CASE("staff_call_lifeguard.visibility_and_simple_thoughts")
{
    Rig rig;
    rig.lifeguard->m_visible = 1;
    rig.Run(0xb);
    OBL_CHECK_EQ(rig.lifeguard->m_visible, 0);
    rig.Run(0xc);
    OBL_CHECK_EQ(rig.lifeguard->m_visible, 1);
    rig.Run(0x7b);
    OBL_CHECK_EQ(rig.env.log, std::string("thought(20000000) "));
    rig.Run(0x8c);
    OBL_CHECK_EQ(rig.env.log, std::string());
}

OBL_TEST_CASE("staff_call_lifeguard.movement_helpers_are_forwarded")
{
    Rig rig;
    const struct {
        int32_t id;
        const char* log;
    } cases[] = {
            {0x2f, "shore "},
            {0x89, "tower "},
            {0x8a, "beachpoint "},
            {0x8b, "seaentry "},
            {0x8f, "drawbuild "},
            {0x90, "drawserve "},
            {0x91, "beach "},
            {0x98, "object(200) "},
    };
    for (const auto& c : cases) {
        rig.Run(c.id);
        OBL_CHECK_EQ(rig.env.log, std::string(c.log));
    }
}

OBL_TEST_CASE("staff_call_lifeguard.a_message_to_self_carries_the_hotel_id")
{
    Rig rig;
    rig.Run(0x88);
    OBL_CHECK_EQ(rig.env.posted.sender, 100);
    OBL_CHECK_EQ(rig.env.posted.receiver, 100);
    OBL_CHECK_EQ(rig.env.posted.type, 9);
    OBL_CHECK_EQ(rig.env.posted.data, 300);
}

OBL_TEST_CASE("staff_call_lifeguard.the_rescued_guest_is_moved_and_turned")
{
    Rig rig(3, 0, 2);
    rig.lifeguard->m_WorldXYZ[0] = 1;
    rig.lifeguard->m_WorldXYZ[1] = 2;
    rig.lifeguard->m_WorldXYZ[2] = 3;
    rig.Run(0x8d);
    OBL_CHECK_EQ(rig.guest->m_WorldXYZ[1], 2.0f);
    OBL_CHECK_EQ(rig.guestSprite.instance.m_direction, 3);
    rig.Run(0x8e); // 7 - 2 * home direction
    OBL_CHECK_EQ(rig.selfSprite.instance.m_direction, 3);
    rig.homeSprite.instance.m_direction = 4;
    rig.Run(0x8e);
    OBL_CHECK_EQ(rig.selfSprite.instance.m_direction, -1);
    rig.Run(0x96);
    rig.guest->m_visible = 1;
    rig.Run(0x96);
    OBL_CHECK_EQ(rig.guest->m_visible, 0);
    rig.Run(0x97);
    OBL_CHECK_EQ(rig.guest->m_visible, 1);
}

OBL_TEST_CASE("staff_call_lifeguard.drowning_and_saving_counters")
{
    Rig rig;
    rig.lifeguard->m_RequiredFeedback = 5;
    rig.lifeguard->m_FeedbackStatus = 7;
    rig.Run(0x92);
    OBL_CHECK_EQ(rig.lifeguard->m_GuestDrowned, 1);
    OBL_CHECK_EQ(rig.lifeguard->m_NumDrowned, 1);
    OBL_CHECK_EQ(rig.lifeguard->m_timesCalledOut, 1);
    OBL_CHECK_EQ(rig.lifeguard->m_RequiredFeedback, 0);
    OBL_CHECK_EQ(rig.lifeguard->m_FeedbackStatus, 5);
    rig.Run(0x93);
    OBL_CHECK_EQ(rig.lifeguard->m_timesCalledOut, 2);
    OBL_CHECK_EQ(rig.lifeguard->m_NumSaves, 1);
    OBL_CHECK_EQ(rig.env.log, std::string("thought(80) "));
}

OBL_TEST_CASE("staff_call_lifeguard.the_guest_is_in_my_area_when_the_sea_area_matches")
{
    Rig rig;
    rig.lifeguard->m_InfluenceArea = 4;
    rig.env.seaArea = 4;
    rig.Run(0x94);
    OBL_CHECK_EQ(rig.lifeguard->m_inMyArea, 1);
    rig.env.seaArea = 5;
    rig.Run(0x94);
    OBL_CHECK_EQ(rig.lifeguard->m_inMyArea, 0);
}

OBL_TEST_CASE("staff_call_lifeguard.the_rescue_pose_uses_the_offsets_of_the_current_direction")
{
    Rig rig(2, 7);
    rig.lifeguard->m_WorldXYZ[0] = 100;
    rig.lifeguard->m_WorldXYZ[1] = 200;
    rig.lifeguard->m_WorldXYZ[2] = 9;
    rig.Run(0x95);
    OBL_CHECK_EQ(rig.lifeguard->m_WorldXYZ[0], 177.0f);
    OBL_CHECK_EQ(rig.lifeguard->m_WorldXYZ[1], 188.0f);
    OBL_CHECK_EQ(rig.guest->m_WorldXYZ[0], 167.0f); // both derive from the pre-move position
    OBL_CHECK_EQ(rig.guest->m_WorldXYZ[1], 204.0f);
    OBL_CHECK_EQ(rig.selfSprite.instance.m_direction, 4);
    OBL_CHECK_EQ(rig.guestSprite.instance.m_direction, 5); // (7 + 6) % 8
}

OBL_TEST_CASE("staff_call_lifeguard.an_out_of_range_direction_keeps_the_position_and_is_clamped")
{
    Rig rig(9, 0);
    rig.lifeguard->m_WorldXYZ[0] = 10;
    rig.lifeguard->m_WorldXYZ[1] = 20;
    rig.Run(0x95);
    OBL_CHECK_EQ(rig.lifeguard->m_WorldXYZ[0], 10.0f);
    OBL_CHECK_EQ(rig.guest->m_WorldXYZ[1], 20.0f);
    OBL_CHECK_EQ(rig.selfSprite.instance.m_direction, 3); // (9 + 2) % 8
    Rig few(3, 0);
    few.selfSprite.anim.m_directions = 4;
    few.Run(0x95);
    OBL_CHECK_EQ(few.selfSprite.instance.m_direction, 3); // (3 + 2) % 8 = 5 is clamped to the last of four directions
    OBL_CHECK_EQ(few.guestSprite.instance.m_direction, 6);
}

OBL_TEST_CASE("staff_call_lifeguard.feedback_functions")
{
    Rig rig;
    rig.lifeguard->m_RequiredFeedback = 6;
    rig.lifeguard->m_FeedbackStatus = 7;
    rig.Run(0xad);
    OBL_CHECK_EQ(rig.lifeguard->m_RequiredFeedback, 0);
    OBL_CHECK_EQ(rig.lifeguard->m_FeedbackStatus, 3);
    OBL_CHECK_EQ(rig.env.log, std::string("base(173) "));
    rig.Run(0xae);
    OBL_CHECK_EQ(rig.lifeguard->m_waitingForDeath, 1);
    rig.Run(0xaf);
    OBL_CHECK_EQ(rig.env.log, std::string("blink(LifeguardIcon,1) "));
}

OBL_TEST_CASE("staff_call_lifeguard.selecting_opens_the_tab_then_shows_the_thought")
{
    Rig rig;
    rig.lifeguard->m_RequiredFeedback = 0x40;
    rig.Run(0xb0);
    OBL_CHECK_EQ(rig.lifeguard->m_CurrentFeedback, 0x40);
    OBL_CHECK_EQ(rig.lifeguard->m_RequiredFeedback, 0);
    OBL_CHECK_EQ(rig.env.log, std::string("select tab(LifeguardDialog,2) thought(40) blink(LifeguardIcon,0) "));
}

OBL_TEST_CASE("staff_call_lifeguard.a_feedback_key_is_added_only_when_known")
{
    Rig rig;
    rig.lifeguard->m_RequiredFeedback = 0x10;
    rig.Run(0xb1);
    OBL_CHECK_EQ(rig.env.log, std::string("key(16) thought(10) blink(LifeguardIcon,0) "));
    rig.env.feedbackKey = "KEY";
    rig.lifeguard->m_RequiredFeedback = 0x10;
    rig.Run(0xb1);
    OBL_CHECK_EQ(rig.env.log, std::string("key(16) feedback(KEY,100,8) thought(10) blink(LifeguardIcon,0) "));
}

OBL_TEST_CASE("staff_call_lifeguard.unrecognized_ids_go_to_the_shared_dispatcher")
{
    Rig rig;
    for (int32_t id : {0x76, 0xa8, 0x99, 0, -1}) {
        rig.Run(id);
        OBL_CHECK_EQ(rig.env.log, "base(" + std::to_string(id) + ") ");
    }
}

} // namespace
