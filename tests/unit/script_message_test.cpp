// The scenario interpreter's message commands (src/game/objects/script_message.h): message words, send plans, state lookup.

#include "unit_test.h"

#include "game/objects/script_message.h"

#include <cmath>
#include <cstring>
#include <limits>

using namespace obl::script;
using obl::script::ScriptValue;

namespace {

ScriptValue Byte(int32_t bits)
{
    return {kTypeByte, bits};
}
ScriptValue Int(int32_t value)
{
    return {kTypeInt, value};
}
ScriptValue Float(float value)
{
    return {kTypeFloat, FloatToBits(value)};
}

} // namespace

OBL_TEST_CASE("script_message.ftol_wraps_like_the_crt")
{
    OBL_CHECK_EQ(FloatToIntWrap(3.9), 3);
    OBL_CHECK_EQ(FloatToIntWrap(-3.9), -3);
    OBL_CHECK_EQ(FloatToIntWrap(4294967296.0), 0); // 2^32: the low 32 bits of the int64
    OBL_CHECK_EQ(FloatToIntWrap(4294967297.0), 1);
    OBL_CHECK_EQ(FloatToIntWrap(std::numeric_limits<double>::quiet_NaN()), 0);
    OBL_CHECK_EQ(FloatToIntWrap(1e30), 0); // beyond int64: the integer indefinite
    OBL_CHECK_EQ(FloatToIntWrap(-2147483649.0), 2147483647); // wraps, no saturation
}

OBL_TEST_CASE("script_message.words_follow_the_kind")
{
    OBL_CHECK_EQ(MessageWord(Byte(0x12345678)), 0x78);
    OBL_CHECK_EQ(MessageWord(Int(-5)), -5);
    OBL_CHECK_EQ(MessageWord(Float(7.75f)), 7);
    OBL_CHECK_EQ(MessageWord(Float(-7.75f)), -7);
    // A string pointer is no float but is converted as one: the bits of a plausible pointer are a tiny denormal.
    OBL_CHECK_EQ(MessageWord({1, 0x00182e80}), 0);
}

OBL_TEST_CASE("script_message.build_maps_operands_to_words")
{
    const BuiltMessage built = BuildMessage(38, Int(11), Float(2.5f), Byte(0x1ff));
    OBL_CHECK_EQ(built.sender, -1);
    OBL_CHECK_EQ(built.receiver, 11);
    OBL_CHECK_EQ(built.type, 38);
    OBL_CHECK_EQ(built.data, 2);
    OBL_CHECK_EQ(built.flags, 0xff);
}

OBL_TEST_CASE("script_message.zero_or_nan_counts_as_zero")
{
    OBL_CHECK(IsZeroOrNaN(0.0f));
    OBL_CHECK(IsZeroOrNaN(-0.0f));
    OBL_CHECK(IsZeroOrNaN(std::numeric_limits<float>::quiet_NaN()));
    OBL_CHECK(!IsZeroOrNaN(1e-30f));
    OBL_CHECK(!IsZeroOrNaN(-2.0f));
}

OBL_TEST_CASE("script_message.send_plan_without_max_time")
{
    int draws = 0;
    auto draw = [&draws]() { return ++draws; };
    SendPlan plan = PlanMessageSend(0.0f, 0.0f, draw);
    OBL_CHECK(!plan.delayed);
    plan = PlanMessageSend(1.5f, 0.0f, draw);
    OBL_CHECK(plan.delayed);
    OBL_CHECK_EQ(plan.delay, 1.5f);
    plan = PlanMessageSend(-2.0f, 0.0f, draw);
    OBL_CHECK(plan.delayed);
    OBL_CHECK_EQ(plan.delay, -2.0f);
    plan = PlanMessageSend(std::numeric_limits<float>::quiet_NaN(), 0.0f, draw);
    OBL_CHECK(!plan.delayed); // unordered compares as equal
    plan = PlanMessageSend(1.0f, std::numeric_limits<float>::quiet_NaN(), draw);
    OBL_CHECK(plan.delayed);
    OBL_CHECK_EQ(plan.delay, 1.0f);
    OBL_CHECK_EQ(draws, 0); // no MaxTime, no draw
}

OBL_TEST_CASE("script_message.max_time_draws_once_and_scales_by_1_over_32767")
{
    int draws = 0;
    auto draw = [&draws](int value) {
        return [&draws, value]() {
            ++draws;
            return value;
        };
    };
    // rand() = 0 gives Time, rand() = 32767 gives (almost exactly) MaxTime: the constant is 1/32767, not 1/32768.
    SendPlan low = PlanMessageSend(1.0f, 3.0f, draw(0));
    OBL_CHECK(low.delayed);
    OBL_CHECK_EQ(low.delay, 1.0f);
    SendPlan high = PlanMessageSend(1.0f, 3.0f, draw(32767));
    OBL_CHECK(std::fabs(high.delay - 3.0f) < 1e-5f);
    SendPlan middle = PlanMessageSend(0.0f, 2.0f, draw(16384));
    OBL_CHECK(std::fabs(middle.delay - 1.0000305f) < 1e-6f);
    OBL_CHECK_EQ(draws, 3);
    // MaxTime below Time runs backwards but still draws.
    SendPlan backwards = PlanMessageSend(3.0f, 1.0f, draw(32767));
    OBL_CHECK(std::fabs(backwards.delay - 1.0f) < 1e-5f);
}

OBL_TEST_CASE("script_message.state_lookup_is_exact_and_not_found_is_minus_one")
{
    const char* names[] = {"STATE_A", "STATE_B", "state_b"};
    OBL_CHECK_EQ(FindStateIndex(names, 3, "STATE_B"), 1);
    OBL_CHECK_EQ(FindStateIndex(names, 3, "state_b"), 2); // case-sensitive
    OBL_CHECK_EQ(FindStateIndex(names, 3, "STATE_C"), kStateNotFound);
    OBL_CHECK_EQ(FindStateIndex(names, 3, ""), kStateNotFound);
    OBL_CHECK_EQ(FindStateIndex(names, 0, "STATE_A"), kStateNotFound);
    OBL_CHECK_EQ(FindStateIndex(names, 1, "STATE_B"), kStateNotFound); // only the states that exist
}

OBL_TEST_CASE("script_message.change_state_plan")
{
    OBL_CHECK(!PlanChangeStateSend(0.0f).delayed);
    OBL_CHECK(!PlanChangeStateSend(std::numeric_limits<float>::quiet_NaN()).delayed);
    OBL_CHECK(PlanChangeStateSend(2.0f).delayed);
    OBL_CHECK_EQ(PlanChangeStateSend(2.0f).delay, 2.0f);
}
