#include "unit_test.h"

#include "game/objects/guest_animation.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

using namespace obl::guest_anim;

namespace {

const Names kNames{"D_MESS", "D_WALK_NORMAL", "D_STAND_DRUNK", "D_RETURN"};

std::string ResolveName(const char* name, const Names& names = kNames)
{
    char scratch[kScratchSize];
    return Resolve(name, names, scratch);
}

} // namespace

OBL_TEST_CASE("guest_anim.special_names_stand_for_the_guests_animations_and_are_case_sensitive")
{
    OBL_CHECK_EQ(ResolveName("WalkAnimation"), std::string("D_WALK_NORMAL"));
    OBL_CHECK_EQ(ResolveName("StandAnimation"), std::string("D_STAND_DRUNK"));
    OBL_CHECK_EQ(ResolveName("ReturnAnimation"), std::string("D_RETURN"));
    OBL_CHECK_EQ(ResolveName("walkanimation"), std::string("walkanimation"));
    OBL_CHECK_EQ(ResolveName("D_SIT"), std::string("D_SIT"));
}

OBL_TEST_CASE("guest_anim.the_message_name_is_resolved_again")
{
    OBL_CHECK_EQ(ResolveName("MessData"), std::string("D_MESS"));
    const Names chained{"WalkAnimation", "D_W", "D_S", "D_R"};
    OBL_CHECK_EQ(ResolveName("MessData", chained), std::string("D_W"));
    const Names fidget{"fidgetanimation1", "D_W", "D_S", "D_R"};
    OBL_CHECK_EQ(ResolveName("MessData", fidget), std::string("D_S_FIDGET1"));
}

OBL_TEST_CASE("guest_anim.fidgets_hang_on_the_stand_animation_and_ignore_case")
{
    OBL_CHECK_EQ(ResolveName("FidgetAnimation1"), std::string("D_STAND_DRUNK_FIDGET1"));
    OBL_CHECK_EQ(ResolveName("FIDGETANIMATION2"), std::string("D_STAND_DRUNK_FIDGET2"));
    const Names normal{"", "", "d_stand_normal", ""};
    OBL_CHECK_EQ(ResolveName("fidgetanimation1", normal), std::string("D_STAND_FIDGET1"));
    OBL_CHECK_EQ(ResolveName("FidgetAnimation2", normal), std::string("D_STAND_FIDGET2"));
}

OBL_TEST_CASE("guest_anim.the_longest_fidget_fits_the_scratch_buffer")
{
    const std::string stand(39, 'S');
    const Names longest{"", "", stand.c_str(), ""};
    OBL_CHECK_EQ(ResolveName("FidgetAnimation1", longest), stand + "_FIDGET1");
}

OBL_TEST_CASE("guest_anim.return_to_is_the_name_or_the_finish_with_two_aliases")
{
    OBL_CHECK_EQ(std::string(ReturnTo("D_PLAY", nullptr, kNames)), std::string("D_PLAY"));
    OBL_CHECK_EQ(std::string(ReturnTo("D_PLAY", "WalkAnimation", kNames)), std::string("D_WALK_NORMAL"));
    OBL_CHECK_EQ(std::string(ReturnTo("D_PLAY", "StandAnimation", kNames)), std::string("D_STAND_DRUNK"));
    OBL_CHECK_EQ(std::string(ReturnTo("D_PLAY", "ReturnAnimation", kNames)), std::string("ReturnAnimation"));
    OBL_CHECK_EQ(std::string(ReturnTo("D_PLAY", "D_FIN", kNames)), std::string("D_FIN"));
}

OBL_TEST_CASE("guest_anim.reverse_wins_over_looping")
{
    OBL_CHECK_EQ(AnimType(false, true), kTypeLoop);
    OBL_CHECK_EQ(AnimType(true, true), kTypeReverse);
    OBL_CHECK_EQ(AnimType(true, false), kTypeReverse);
    OBL_CHECK_EQ(AnimType(false, false), kTypeOnce);
}

OBL_TEST_CASE("guest_anim.a_zero_or_unordered_duration_becomes_one")
{
    OBL_CHECK_EQ(Duration(0.0f), 1.0f);
    OBL_CHECK_EQ(Duration(-0.0f), 1.0f);
    OBL_CHECK_EQ(Duration(std::numeric_limits<float>::quiet_NaN()), 1.0f);
    OBL_CHECK_EQ(Duration(2.5f), 2.5f);
    OBL_CHECK_EQ(Duration(-3.0f), -3.0f);
}

OBL_TEST_CASE("guest_anim.the_end_messages_follow_the_state_and_the_return_animation")
{
    OBL_CHECK_EQ(LeaveState(kNoState, 7), 7);
    OBL_CHECK_EQ(LeaveState(9, 7), 9);
    OBL_CHECK(SendsEndState(false, 7));
    OBL_CHECK(!SendsEndState(true, 7));
    OBL_CHECK(!SendsEndState(false, kEndStateDefault));
    OBL_CHECK(!SendsDelayedLeave(7, 7, true));
    OBL_CHECK(SendsDelayedLeave(7, 7, false));
    OBL_CHECK(SendsDelayedLeave(9, 7, true));
}
