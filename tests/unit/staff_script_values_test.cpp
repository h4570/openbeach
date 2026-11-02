#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "unit_test.h"

#include "game/objects/object_building.h"
#include "game/objects/staff_script_values.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::layout;
using obl::messages::Message;
using obl::object::Building;
using obl::script::ScriptValue;
namespace ex = obl::script_program::exec;

namespace {

float Bits(int32_t bits)
{
    return script::BitsToFloat(bits);
}
int32_t FloatBits(float value)
{
    return script::FloatToBits(value);
}

// A staff member with the message the variables read and a home building (id 7).
struct Fixture {
    std::unique_ptr<object::Staff> staff = std::make_unique<object::Staff>();
    std::unique_ptr<Building> building = std::make_unique<Building>();
    Message message{};

    Fixture()
    {
        obl::unit::FillObject<object::Staff>(*staff);
        obl::unit::FillObject(*building);
        staff->m_currentMessage = &message;
        staff->m_HomeID = 7;
        building->m_id = 7;
    }
};

// A fake of the Env of staff_script_values.h that records what the base class was asked.
struct FakeEnv {
    Fixture& f;
    std::vector<int32_t> baseKeys;
    std::vector<std::string> log;
    bool open = true;
    float dayTime = 12.0f;
    float serve[2] = {100.0f, 200.0f};

    explicit FakeEnv(Fixture& fixture) : f(fixture) {}

    object::Base* Object(int32_t id)
    {
        log.push_back("object " + std::to_string(id));
        return id == 7 ? f.building.get() : nullptr;
    }
    bool IsOpen(object::Base*)
    {
        log.push_back("open");
        return open;
    }
    void ServePosition(object::Base*, float& x, float& y)
    {
        log.push_back("serve");
        x = serve[0];
        y = serve[1];
    }
    float GameDayTime() { return dayTime; }
    ScriptValue Base(object::Staff&, int32_t key, intptr_t data)
    {
        baseKeys.push_back(key);
        return {key, static_cast<int32_t>(data)};
    }
    void Base(object::Staff&, int32_t key, intptr_t, int32_t, int32_t) { baseKeys.push_back(key); }
    int32_t FloatToIntWrap(int32_t floatBits) { return static_cast<int32_t>(Bits(floatBits)); }
};

ScriptValue Get(Fixture& f, FakeEnv& env, int32_t key, intptr_t data = 0)
{
    return staff::GetValue(*f.staff, key, data, env);
}
void Set(Fixture& f, FakeEnv& env, int32_t key, int32_t type, int32_t bits)
{
    staff::SetValue(*f.staff, key, 0, type, bits, env);
}

float Nan()
{
    return std::numeric_limits<float>::quiet_NaN();
}

} // namespace

OBL_TEST_CASE("staff_script_values.handled_numbers_are_exactly_the_ones_not_passed_to_the_base_class")
{
    Fixture f;
    std::vector<int32_t> keys = {INT32_MIN, -1, -0xac, 0x7fffffff};
    for (int32_t key = 0; key <= 0x120; ++key) keys.push_back(key);
    for (int32_t key : keys) {
        FakeEnv get(f);
        const ScriptValue value = staff::GetValue(*f.staff, key, 0x1234, get);
        OBL_CHECK_EQ(get.baseKeys.empty(), staff::HandlesGet(key));
        if (!staff::HandlesGet(key)) { // the base class's answer comes back unchanged
            OBL_CHECK_EQ(value.type, key);
            OBL_CHECK_EQ(value.bits, 0x1234);
        }
        FakeEnv set(f);
        staff::SetValue(*f.staff, key, 0, 2, 3, set);
        OBL_CHECK_EQ(set.baseKeys.empty(), staff::HandlesSet(key));
    }
}

OBL_TEST_CASE("staff_script_values.message_variables_read_the_current_message")
{
    Fixture f;
    FakeEnv env(f);
    f.message.sender = 41;
    f.message.data = 42;
    f.message.flags = 43;
    f.message.extra = 44;
    OBL_CHECK_EQ(Get(f, env, 0x00).bits, 41);
    OBL_CHECK_EQ(Get(f, env, 0x05).bits, 42);
    OBL_CHECK_EQ(Get(f, env, 0x2b).bits, 43);
    OBL_CHECK_EQ(Get(f, env, 0x5d).bits, 44);
    OBL_CHECK_EQ(Get(f, env, 0x5d).type, ex::kTypeInt);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_script_values.fields_come_back_with_their_types")
{
    Fixture f;
    FakeEnv env(f);
    f.staff->m_StartTime = 8;
    f.staff->m_EndTime = 17;
    f.staff->m_DirectStaff = 0x5a;
    f.staff->m_GotoObject = -3;
    f.staff->m_foundDirecting = 1;
    f.staff->m_IdleTimePerHour = 12;
    f.staff->m_LastAction = 77;
    OBL_CHECK_EQ(Get(f, env, 0x7b).bits, 8);
    OBL_CHECK_EQ(Get(f, env, 0x7c).bits, 17);
    OBL_CHECK_EQ(Get(f, env, 0x7f).bits, -3);
    OBL_CHECK_EQ(Get(f, env, 0x83).bits, 12);
    OBL_CHECK_EQ(Get(f, env, 0x84).bits, 77);
    for (int32_t key : {0x7b, 0x7c, 0x7f, 0x83, 0x84}) OBL_CHECK_EQ(Get(f, env, key).type, ex::kTypeInt);
    OBL_CHECK_EQ(Get(f, env, 0x7e).type, ex::kTypeByte);
    OBL_CHECK_EQ(Get(f, env, 0x7e).bits & 0xff, 0x5a);
    OBL_CHECK_EQ(Get(f, env, 0x80).type, ex::kTypeByte);
    OBL_CHECK_EQ(Get(f, env, 0x80).bits & 0xff, 1);
}

OBL_TEST_CASE("staff_script_values.positions_are_truncated_like_the_games_truncation")
{
    Fixture f;
    FakeEnv env(f);
    f.staff->m_WanderPos[0] = 12.9f;
    f.staff->m_WanderPos[1] = -12.9f;
    f.staff->m_DirectPos[0] = 4294967296.0f; // past the int range: the low 32 bits of the 64-bit truncation
    f.staff->m_DirectPos[1] = 2147483648.0f;
    OBL_CHECK_EQ(Get(f, env, 0x74).bits, 12);
    OBL_CHECK_EQ(Get(f, env, 0x75).bits, -12);
    OBL_CHECK_EQ(Get(f, env, 0x81).bits, 0);
    OBL_CHECK_EQ(Get(f, env, 0x82).bits, static_cast<int32_t>(0x80000000u));
    f.staff->m_DirectPos[0] = Nan(); // the integer indefinite of the 64-bit conversion has zero low 32 bits
    f.staff->m_DirectPos[1] = 2.0e19f; // beyond 64 bits
    OBL_CHECK_EQ(Get(f, env, 0x81).bits, 0);
    OBL_CHECK_EQ(Get(f, env, 0x82).bits, 0);
    for (int32_t key : {0x74, 0x75, 0x81, 0x82}) OBL_CHECK_EQ(Get(f, env, key).type, ex::kTypeInt);
}

OBL_TEST_CASE("staff_script_values.at_work_needs_a_home_the_shift_and_an_open_building")
{
    Fixture f;
    FakeEnv env(f);
    f.staff->m_StartTime = 8;
    f.staff->m_EndTime = 17;
    env.dayTime = 12.5f;
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 1);
    OBL_CHECK_EQ(Get(f, env, 0x7d).type, ex::kTypeInt);
    env.open = false; // closed
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 0);
    env.open = true;
    f.staff->m_HomeID = 8; // no such building: the clock and the building are not asked
    env.log.clear();
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 0);
    OBL_CHECK_EQ(env.log.size(), size_t(1));
    f.staff->m_HomeID = 7;
    for (float hour : {7.99f, 17.0f, 17.5f, 23.0f, 0.0f}) { // outside [8, 17)
        env.dayTime = hour;
        OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 0);
    }
    for (float hour : {8.0f, 8.01f, 16.999f}) {
        env.dayTime = hour;
        OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 1);
    }
}

OBL_TEST_CASE("staff_script_values.at_work_through_midnight_and_unsigned_hours")
{
    Fixture f;
    FakeEnv env(f);
    f.staff->m_StartTime = 22;
    f.staff->m_EndTime = 6; // the night shift
    for (float hour : {22.0f, 23.5f, 0.0f, 5.99f}) {
        env.dayTime = hour;
        OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 1);
    }
    for (float hour : {6.0f, 12.0f, 21.99f}) {
        env.dayTime = hour;
        OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 0);
    }
    // The hours are compared unsigned: a negative clock is a huge number, a negative start a huge start.
    f.staff->m_StartTime = 8;
    f.staff->m_EndTime = -1;
    env.dayTime = -1.0f;
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 0); // 0xffffffff is not below 0xffffffff
    env.dayTime = 100.0f;
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 1);
    f.staff->m_StartTime = 0;
    f.staff->m_EndTime = 0; // an empty window
    env.dayTime = 0.0f;
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 0);
    f.staff->m_StartTime = 9;
    f.staff->m_EndTime = 9;
    env.dayTime = 9.0f;
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 0);
    env.dayTime = Nan(); // the integer indefinite: hour 0
    f.staff->m_StartTime = 0;
    f.staff->m_EndTime = 1;
    OBL_CHECK_EQ(Get(f, env, 0x7d).bits, 1);
}

OBL_TEST_CASE("staff_script_values.at_home_looks_at_the_truncated_distance_to_the_serve_point")
{
    Fixture f;
    FakeEnv env(f);
    f.staff->m_WorldXYZ[0] = 100.0f;
    f.staff->m_WorldXYZ[1] = 200.0f;
    ScriptValue value = Get(f, env, 0xac);
    OBL_CHECK_EQ(value.type, ex::kTypeByte);
    OBL_CHECK_EQ(value.bits & 0xff, 1);
    f.staff->m_WorldXYZ[0] = 100.9f; // truncates to 0 both ways
    f.staff->m_WorldXYZ[1] = 199.1f;
    OBL_CHECK_EQ(Get(f, env, 0xac).bits & 0xff, 1);
    f.staff->m_WorldXYZ[0] = 98.5f; // 1.5 -> 1: not near
    f.staff->m_WorldXYZ[1] = 200.0f;
    OBL_CHECK_EQ(Get(f, env, 0xac).bits & 0xff, 0);
    f.staff->m_WorldXYZ[0] = 100.0f;
    f.staff->m_WorldXYZ[1] = 202.0f; // y off
    OBL_CHECK_EQ(Get(f, env, 0xac).bits & 0xff, 0);
    f.staff->m_WorldXYZ[1] = -3.0e9f; // 200 + 3e9 wraps into the 32-bit range as 3000000200 -> negative -> large: not near
    OBL_CHECK_EQ(Get(f, env, 0xac).bits & 0xff, 0);
}

OBL_TEST_CASE("staff_script_values.at_home_most_negative_difference_counts_as_near")
{
    Fixture f;
    FakeEnv env(f);
    env.serve[0] = 0.0f;
    f.staff->m_WorldXYZ[0] = 2147483648.0f; // serve - own = -2^31: the absolute value stays -2^31, which is below 0.1
    f.staff->m_WorldXYZ[1] = 200.0f;
    OBL_CHECK_EQ(Get(f, env, 0xac).bits & 0xff, 1);
}

OBL_TEST_CASE("staff_script_values.at_home_with_a_nan_position_is_near_on_that_axis")
{
    Fixture f;
    FakeEnv env(f);
    f.staff->m_WorldXYZ[0] = 150.0f; // x far
    f.staff->m_WorldXYZ[1] = Nan();
    OBL_CHECK_EQ(Get(f, env, 0xac).bits & 0xff, 0);
    f.staff->m_WorldXYZ[0] = 100.0f; // x near: the NaN difference truncates to 0 (the integer indefinite), so y is near too
    OBL_CHECK_EQ(Get(f, env, 0xac).bits & 0xff, 1);
}

OBL_TEST_CASE("staff_script_values.set_walk_speed_stores_the_wide_float_conversion")
{
    Fixture f;
    FakeEnv env(f);
    Set(f, env, 0x47, ex::kTypeFloat, FloatBits(2.5f));
    OBL_CHECK_EQ(f.staff->m_maxVel, 2.5f);
    Set(f, env, 0x47, ex::kTypeInt, 7);
    OBL_CHECK_EQ(f.staff->m_maxVel, 7.0f);
    Set(f, env, 0x47, ex::kTypeInt, -1); // an int is read unsigned
    OBL_CHECK_EQ(f.staff->m_maxVel, 4294967296.0f);
    Set(f, env, 0x47, ex::kTypeByte, 0x1ff);
    OBL_CHECK_EQ(f.staff->m_maxVel, 255.0f);
    Set(f, env, 0x47, ex::kTypeText, 0x12345678);
    OBL_CHECK_EQ(f.staff->m_maxVel, 0.0f);
    OBL_CHECK(env.baseKeys.empty());
}

OBL_TEST_CASE("staff_script_values.set_found_directing_reads_the_bits_as_a_float")
{
    Fixture f;
    FakeEnv env(f);
    for (int32_t type : {ex::kTypeFloat, ex::kTypeInt, ex::kTypeByte, ex::kTypeText}) {
        Set(f, env, 0x80, type, FloatBits(1.0f));
        OBL_CHECK_EQ(f.staff->m_foundDirecting, 1);
        Set(f, env, 0x80, type, FloatBits(0.5f));
        OBL_CHECK_EQ(f.staff->m_foundDirecting, 0);
    }
    // The threshold is the double 0.9: the float nearest to it is just below.
    Set(f, env, 0x80, ex::kTypeFloat, FloatBits(0.9f));
    OBL_CHECK_EQ(f.staff->m_foundDirecting, 0);
    Set(f, env, 0x80, ex::kTypeFloat, FloatBits(0.90000004f));
    OBL_CHECK_EQ(f.staff->m_foundDirecting, 1);
    Set(f, env, 0x80, ex::kTypeFloat, FloatBits(Nan())); // unordered counts as "not above"
    OBL_CHECK_EQ(f.staff->m_foundDirecting, 0);
    Set(f, env, 0x80, ex::kTypeInt, 7); // an integer as bits is a denormal: false
    OBL_CHECK_EQ(f.staff->m_foundDirecting, 0);
    Set(f, env, 0x80, ex::kTypeFloat, FloatBits(std::numeric_limits<float>::infinity()));
    OBL_CHECK_EQ(f.staff->m_foundDirecting, 1);
}

OBL_TEST_CASE("staff_script_values.set_last_action_and_debug_info_convert_through_to_int")
{
    Fixture f;
    FakeEnv env(f);
    Set(f, env, 0x84, ex::kTypeInt, 12);
    OBL_CHECK_EQ(f.staff->m_LastAction, 12);
    Set(f, env, 0x84, ex::kTypeFloat, FloatBits(-3.9f));
    OBL_CHECK_EQ(f.staff->m_LastAction, -3);
    Set(f, env, 0x84, ex::kTypeByte, 0x1ff);
    OBL_CHECK_EQ(f.staff->m_LastAction, 255);
    Set(f, env, 0x84, ex::kTypeText, 0x1000);
    OBL_CHECK_EQ(f.staff->m_LastAction, 0);
    Set(f, env, 0x105, ex::kTypeInt, -9);
    OBL_CHECK_EQ(f.staff->m_DebugInfo, -9);
    Set(f, env, 0x105, ex::kTypeFloat, FloatBits(5.5f));
    OBL_CHECK_EQ(f.staff->m_DebugInfo, 5);
    OBL_CHECK_EQ(f.staff->m_LastAction, 0);
    OBL_CHECK(env.baseKeys.empty());
}
