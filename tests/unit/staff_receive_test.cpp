#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/world/world_state.h"
#include "unit_test.h"
#include "byte_copy.h"

#include "game/objects/staff_receive.h"

#include <cmath>
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
using namespace obl::tables;

namespace {

// The game as a script of answers; every call is written to `log` in order.
struct ReceiveEnv {
    std::string log;
    layout::World world{};
    layout::Player player{};
    object::Base* home = reinterpret_cast<object::Base*>(0x1000); // the object the id leads to (null: none)
    bool open = true;
    float clock = 12.0f;
    bool receipt = false; // the script has a procedure for the message
    bool movement = true; // the answer of the movement code
    object::Staff* member = nullptr;
    Message seen{}; // what m_currentMessage held at the call of the script
    const void* seenPointer = nullptr;
    int32_t askedId = 0;

    ReceiveEnv() { world.m_player.reset(&player); }

    object::Base* Object(int32_t id)
    {
        add("Object(" + std::to_string(id) + ")");
        askedId = id;
        return home;
    }
    bool IsOpen(object::Base*)
    {
        add("IsOpen");
        return open;
    }
    float GameDayTime() { return clock; }
    layout::World& World() { return world; }
    void Push(uint32_t flags) { add("Push(" + hex(flags) + ")"); }
    void Recalculate() { add("Recalculate"); }
    bool RunOnReceipt(const Message& message)
    {
        add("Receipt(" + std::to_string(message.type) + ")");
        Capture();
        return receipt;
    }
    bool Movement(const Message& message)
    {
        add("Movement(" + std::to_string(message.type) + ")");
        return movement;
    }
    void Capture()
    {
        seenPointer = member->m_currentMessage;
        if (seenPointer) std::memcpy(&seen, seenPointer, sizeof(Message));
    }

    static std::string hex(uint32_t value)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%x", value);
        return text;
    }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

std::unique_ptr<object::Staff> Member()
{
    auto member = std::make_unique<object::Staff>();
    obl::unit::FillObject<object::Staff>(*member);
    static StaffDef definition{};
    definition.m_RecommendedWage = 200;
    member->m_staffDefinition = &definition;
    member->m_HomeID = 42;
    member->m_Wage = 100;
    member->m_SalaryExpectation = 200.0f;
    member->m_salaryExpectationGrowth = 1.1f;
    member->m_happiness = 0.5f;
    member->m_StartTime = 9;
    member->m_EndTime = 18;
    return member;
}

Message Of(int type)
{
    Message message{};
    message.type = type;
    message.sender = 7;
    message.receiver = 8;
    message.data = 99;
    return message;
}

} // namespace

OBL_TEST_CASE("staff_receive.new_day_raises_the_expectation_then_recalculates")
{
    auto member = Member();
    ReceiveEnv env;
    env.member = member.get();
    // 200 * 1.1f is the double product of the float growth, minus 200, plus the expectation, stored once as a float
    const double grown = 200.0 * static_cast<double>(1.1f);
    const float expected = static_cast<float>((grown - 200.0) + 200.0);
    staff::ReceiveMessage(*member, Of(MT_NEW_DAY), env);
    OBL_CHECK_EQ(member->m_SalaryExpectation, expected);
    OBL_CHECK_EQ(env.log, std::string("Recalculate Receipt(38) Movement(38)"));
}

OBL_TEST_CASE("staff_receive.new_day_reads_the_recommended_wage_unsigned")
{
    auto member = Member();
    static StaffDef definition{};
    definition.m_RecommendedWage = -1; // 0xffffffff: 4294967295 as the unsigned 64-bit integer
    member->m_staffDefinition = &definition;
    member->m_salaryExpectationGrowth = 1.0f;
    member->m_SalaryExpectation = 5.0f;
    ReceiveEnv env;
    env.member = member.get();
    staff::ReceiveMessage(*member, Of(MT_NEW_DAY), env);
    OBL_CHECK_EQ(member->m_SalaryExpectation, 5.0f); // growth 1: (E - E) + 5
    member->m_salaryExpectationGrowth = 2.0f;
    staff::ReceiveMessage(*member, Of(MT_NEW_DAY), env);
    OBL_CHECK_EQ(member->m_SalaryExpectation, static_cast<float>(4294967295.0 + 5.0));
}

OBL_TEST_CASE("staff_receive.hour_on_shift_in_an_open_building_works_and_is_paid")
{
    auto member = Member();
    member->m_hoursWorkedInRow = 3;
    ReceiveEnv env;
    env.member = member.get();
    env.player.m_cash = 1000.0f;
    env.world.m_costsToday = 10.0f;
    env.world.m_wagesCostToday = 20.0f;
    env.clock = 12.5f;
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(env.log, std::string("Object(42) IsOpen Recalculate Receipt(85) Movement(85)"));
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 4u);
    OBL_CHECK_EQ(env.player.m_cash, 900.0f);
    OBL_CHECK_EQ(env.world.m_costsToday, 110.0f);
    OBL_CHECK_EQ(env.world.m_wagesCostToday, 120.0f);
}

OBL_TEST_CASE("staff_receive.the_eleventh_hour_in_a_row_adds_the_overworked_thought_before_the_pay")
{
    auto member = Member();
    member->m_hoursWorkedInRow = 10;
    ReceiveEnv env;
    env.member = member.get();
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 11u);
    OBL_CHECK_EQ(env.log, std::string("Object(42) IsOpen Push(80000000) Recalculate Receipt(85) Movement(85)"));
    // exactly ten is not enough
    member->m_hoursWorkedInRow = 9;
    env.log.clear();
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(env.log, std::string("Object(42) IsOpen Recalculate Receipt(85) Movement(85)"));
    // the count is signed: -1 + 1 is 0, which is not over ten
    member->m_hoursWorkedInRow = 0xffffffffu;
    env.log.clear();
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 0u);
    OBL_CHECK_EQ(env.log, std::string("Object(42) IsOpen Recalculate Receipt(85) Movement(85)"));
}

OBL_TEST_CASE("staff_receive.hour_off_shift_or_in_a_closed_building_rests_and_never_goes_below_zero")
{
    auto member = Member();
    member->m_hoursWorkedInRow = 5;
    ReceiveEnv env;
    env.member = member.get();
    env.player.m_cash = 1000.0f;
    env.clock = 20.0f; // after the shift
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 4u);
    OBL_CHECK_EQ(env.log, std::string("Object(42) Recalculate Receipt(85) Movement(85)")); // the building is not asked off shift
    OBL_CHECK_EQ(env.player.m_cash, 1000.0f);
    member->m_hoursWorkedInRow = 0;
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 0u);
    // on shift but closed
    member->m_hoursWorkedInRow = 7;
    env.clock = 10.0f;
    env.open = false;
    env.log.clear();
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 6u);
    OBL_CHECK_EQ(env.log, std::string("Object(42) IsOpen Recalculate Receipt(85) Movement(85)"));
    OBL_CHECK_EQ(env.player.m_cash, 1000.0f);
}

OBL_TEST_CASE("staff_receive.shift_window_is_start_inclusive_end_exclusive_and_wraps_midnight")
{
    auto member = Member();
    ReceiveEnv env;
    env.member = member.get();
    const auto Works = [&](float clock) {
        member->m_hoursWorkedInRow = 5;
        env.clock = clock;
        staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
        return member->m_hoursWorkedInRow == 6u;
    };
    OBL_CHECK(!Works(8.99f));
    OBL_CHECK(Works(9.0f));
    OBL_CHECK(Works(17.99f));
    OBL_CHECK(!Works(18.0f));
    member->m_StartTime = 22;
    member->m_EndTime = 6;
    OBL_CHECK(Works(22.0f));
    OBL_CHECK(Works(23.5f));
    OBL_CHECK(Works(0.0f));
    OBL_CHECK(Works(5.99f));
    OBL_CHECK(!Works(6.0f));
    OBL_CHECK(!Works(12.0f));
    member->m_StartTime = 9;
    member->m_EndTime = 9; // an empty shift never works
    OBL_CHECK(!Works(9.0f));
    OBL_CHECK(!Works(0.0f));
    OBL_CHECK(!Works(23.0f));
}

OBL_TEST_CASE("staff_receive.the_hour_is_the_truncated_clock_read_unsigned")
{
    auto member = Member();
    ReceiveEnv env;
    env.member = member.get();
    member->m_StartTime = 0;
    member->m_EndTime = 24;
    // a negative clock truncates to -1 = 0xffffffff: beyond the shift
    env.clock = -1.0f;
    member->m_hoursWorkedInRow = 5;
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 4u);
    // NaN and huge values give the integer indefinite, i.e. 0: inside 0..24
    env.clock = std::numeric_limits<float>::quiet_NaN();
    member->m_hoursWorkedInRow = 5;
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 6u);
    env.clock = 23.99f;
    member->m_hoursWorkedInRow = 5;
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(member->m_hoursWorkedInRow, 6u);
}

OBL_TEST_CASE("staff_receive.idle_minutes_follow_the_happiness_truncated_towards_zero")
{
    auto member = Member();
    ReceiveEnv env;
    env.member = member.get();
    const auto Idle = [&](float happiness) {
        member->m_happiness = happiness;
        staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
        return member->m_IdleTimePerHour;
    };
    OBL_CHECK_EQ(Idle(0.0f), 60);
    OBL_CHECK_EQ(Idle(1.0f), 0);
    OBL_CHECK_EQ(Idle(0.5f), 30);
    OBL_CHECK_EQ(Idle(0.7f), 18); // 0.7f is a little under 0.7, so 60 - 60 * h is a little over 18
    OBL_CHECK_EQ(Idle(0.3f), 41); // 0.3f is a little over 0.3: a little under 42
    OBL_CHECK_EQ(Idle(2.0f), -60);
    OBL_CHECK_EQ(Idle(-1.0f), 120);
    OBL_CHECK_EQ(Idle(std::numeric_limits<float>::quiet_NaN()), 0);
    OBL_CHECK_EQ(Idle(std::numeric_limits<float>::infinity()), 0); // beyond 64 bits: the integer indefinite
}

OBL_TEST_CASE("staff_receive.the_wage_is_signed_for_the_cash_and_unsigned_for_the_counters")
{
    auto member = Member();
    member->m_Wage = -2; // 0xfffffffe
    ReceiveEnv env;
    env.member = member.get();
    env.player.m_cash = 10.0f;
    env.world.m_costsToday = 0.0f;
    env.world.m_wagesCostToday = 5.0f;
    staff::ReceiveMessage(*member, Of(MT_NEW_HOUR), env);
    OBL_CHECK_EQ(env.player.m_cash, 12.0f);
    OBL_CHECK_EQ(env.world.m_costsToday, static_cast<float>(4294967294.0));
    OBL_CHECK_EQ(env.world.m_wagesCostToday, static_cast<float>(4294967294.0 + 5.0));
}

OBL_TEST_CASE("staff_receive.other_messages_go_to_the_script_and_only_unhandled_ones_to_the_movement_code")
{
    auto member = Member();
    ReceiveEnv env;
    env.member = member.get();
    object::Staff before;
    obl::test::ByteCopy(before, *member);
    env.receipt = true;
    OBL_CHECK(staff::ReceiveMessage(*member, Of(0x2c), env));
    OBL_CHECK_EQ(env.log, std::string("Receipt(44)"));
    env.log.clear();
    env.receipt = false;
    env.movement = true;
    OBL_CHECK(staff::ReceiveMessage(*member, Of(5), env));
    OBL_CHECK_EQ(env.log, std::string("Receipt(5) Movement(5)"));
    env.log.clear();
    env.movement = false;
    OBL_CHECK(!staff::ReceiveMessage(*member, Of(5), env));
    OBL_CHECK_EQ(std::memcmp(&before, member.get(), sizeof(object::Staff)), 0);
}

OBL_TEST_CASE("staff_receive.the_current_message_is_a_copy_during_the_calls_and_is_put_back")
{
    auto member = Member();
    ReceiveEnv env;
    env.member = member.get();
    Message message = Of(5);
    message.flags = 3;
    message.extra = 14;
    message.floatData = 2.5f;
    message.time = 7.0f;
    int marker = 0;
    member->m_currentMessage = &marker;
    staff::ReceiveMessage(*member, message, env);
    OBL_CHECK(env.seenPointer != nullptr);
    OBL_CHECK(env.seenPointer != &message);
    OBL_CHECK_EQ(env.seen.sender, 7);
    OBL_CHECK_EQ(env.seen.type, 5);
    OBL_CHECK_EQ(env.seen.flags, 3);
    OBL_CHECK_EQ(env.seen.data, 99);
    OBL_CHECK_EQ(env.seen.extra, 14);
    OBL_CHECK_EQ(env.seen.floatData, 2.5f);
    OBL_CHECK(member->m_currentMessage == &marker);
    member->m_currentMessage = nullptr;
    staff::ReceiveMessage(*member, message, env);
    OBL_CHECK(member->m_currentMessage == nullptr);
}

OBL_TEST_CASE("staff_receive.the_copy_of_the_message_quiets_a_signalling_nan_in_the_float_word")
{
    auto member = Member();
    ReceiveEnv env;
    env.member = member.get();
    Message message = Of(5);
    const uint32_t signalling = 0x7fa00001u;
    std::memcpy(&message.floatData, &signalling, sizeof(signalling));
    staff::ReceiveMessage(*member, message, env);
    uint32_t seen;
    std::memcpy(&seen, &env.seen.floatData, sizeof(seen));
    OBL_CHECK_EQ(seen, 0x7fe00001u);
    uint32_t bits;
    std::memcpy(&bits, &message.floatData, sizeof(bits));
    OBL_CHECK_EQ(bits, signalling);
    // an ordinary value and a quiet NaN are untouched
    message.floatData = -3.0f;
    staff::ReceiveMessage(*member, message, env);
    OBL_CHECK_EQ(env.seen.floatData, -3.0f);
}
