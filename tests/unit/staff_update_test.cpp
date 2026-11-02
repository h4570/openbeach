#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"

#include "game/objects/staff_update.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::world_data;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using namespace obl::tables;

namespace {

// The game as a script of answers; every call is written to `log` in order.
struct UpdateEnv {
    std::string log;
    bool humanOk = true;
    uint32_t messages = 0; // feedback::Feedback::StaffMessages(6); MarkStaffMessage adds to it
    int32_t dayLength = 240;
    float frame = 0.02f;
    std::string fatalName;

    bool HumanUpdate()
    {
        add("Human");
        return humanOk;
    }
    void Fatal(const char* name)
    {
        add("Fatal");
        fatalName = name;
    }
    const char* Key(uint32_t flags)
    {
        add("Key(" + hex(flags) + ")");
        return "text";
    }
    uint32_t StaffMessages(int type)
    {
        add("Get(" + hex(static_cast<uint32_t>(type)) + ")");
        return messages;
    }
    void Mark(uint32_t bits, int type)
    {
        add("Mark(" + hex(bits) + "," + hex(static_cast<uint32_t>(type)) + ")");
        messages |= bits;
    }
    void Add(const char* key, int32_t id, uint32_t type)
    {
        add(std::string("Add(") + key + "," + std::to_string(id) + "," + hex(type) + ")");
    }
    void Push(uint32_t flags) { add("Push(" + hex(flags) + ")"); }
    int32_t DayLengthSeconds() { return dayLength; }
    float GameDeltaTime() { return frame; }

    static std::string hex(uint32_t value)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%x", value);
        return text;
    }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

std::unique_ptr<object::Staff> Member(float happiness, float target, float rate, bool changing)
{
    auto member = std::make_unique<object::Staff>();
    obl::unit::FillObject<object::Staff>(*member);
    std::strcpy(member->m_Name, "Joe");
    member->m_id = 77;
    member->m_happiness = happiness;
    member->m_targetHappiness = target;
    member->m_happinessRate = rate;
    member->m_happinessChanging = changing ? 1 : 0;
    return member;
}

} // namespace

OBL_TEST_CASE("staff_update.runs_the_base_update_first_and_ends_the_game_when_it_fails")
{
    auto member = Member(0.5f, 0.5f, 0.0f, false);
    UpdateEnv env;
    OBL_CHECK(staff::UpdateStaff(*member, env));
    OBL_CHECK_EQ(env.log, std::string("Human"));
    env.log.clear();
    env.humanOk = false;
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Human Fatal"));
    OBL_CHECK_EQ(env.fatalName, std::string("Joe"));
}

OBL_TEST_CASE("staff_update.flag_thought_is_announced_once_and_clears_the_flag")
{
    auto member = Member(0.5f, 0.5f, 0.0f, false);
    member->m_directedToObject = 1;
    UpdateEnv env;
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Human Key(200000) Add(text,77,7) Mark(200000,6) Push(200000)"));
    OBL_CHECK_EQ(member->m_directedToObject, 0);
    env.log.clear();
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Human"));
}

OBL_TEST_CASE("staff_update.sacked_member_is_announced_once_for_the_whole_staff")
{
    auto member = Member(0.5f, 0.5f, 0.0f, false);
    member->m_gotTheSack = 1;
    UpdateEnv env;
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Human Get(6) Key(10000000) Add(text,77,7) Mark(10000000,6) Push(10000000)"));
    OBL_CHECK_EQ(member->m_gotTheSack, 1); // the member stays sacked; the mask of the manager stops the repeat
    env.log.clear();
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Human Get(6)"));
}

OBL_TEST_CASE("staff_update.both_announcements_in_one_frame_come_in_order")
{
    auto member = Member(0.5f, 0.5f, 0.0f, false);
    member->m_directedToObject = 9;
    member->m_gotTheSack = 1;
    UpdateEnv env;
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(env.log,
            std::string("Human Key(200000) Add(text,77,7) Mark(200000,6) Push(200000) Get(6) Key(10000000) Add(text,77,7) "
                        "Mark(10000000,6) Push(10000000)"));
    // a mask that already holds the sacked bit (an unrelated bit does not hide it)
    member->m_directedToObject = 0;
    env.log.clear();
    env.messages = 0x20000000;
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(env.log, std::string("Human Get(6) Key(10000000) Add(text,77,7) Mark(10000000,6) Push(10000000)"));
}

OBL_TEST_CASE("staff_update.happiness_walks_by_rate_per_game_hour")
{
    // 240 s a day: a game hour is 10 s; 0.02 s of it with the rate 0.5 per hour moves the happiness by 0.5 * 0.02 / 10 = 0.001.
    auto member = Member(0.5f, 1.0f, 0.5f, true);
    UpdateEnv env;
    staff::UpdateStaff(*member, env);
    OBL_CHECK(std::fabs(member->m_happiness - 0.501f) < 1e-6f);
    OBL_CHECK_EQ(member->m_happinessChanging, 1);
    OBL_CHECK_EQ(member->m_happinessRate, 0.5f);
    // a negative rate walks down
    auto down = Member(0.5f, 0.0f, -0.5f, true);
    staff::UpdateStaff(*down, env);
    OBL_CHECK(std::fabs(down->m_happiness - 0.499f) < 1e-6f);
}

OBL_TEST_CASE("staff_update.integer_hour_length_truncates_the_day_by_24")
{
    // 250 s / 24 = 10 (not 10.41...): the same step as 240 s
    auto a = Member(0.5f, 1.0f, 0.5f, true);
    auto b = Member(0.5f, 1.0f, 0.5f, true);
    UpdateEnv env;
    env.dayLength = 250;
    staff::UpdateStaff(*a, env);
    env.dayLength = 240;
    staff::UpdateStaff(*b, env);
    OBL_CHECK_EQ(std::memcmp(&a->m_happiness, &b->m_happiness, sizeof(float)), 0);
}

OBL_TEST_CASE("staff_update.walk_ends_when_target_and_happiness_agree_to_the_thousandth")
{
    // 0.5004 and 0.5009 both truncate to 500 thousandths: the walk is over (the rate is zeroed before the step, so the happiness stays)
    auto member = Member(0.5004f, 0.5009f, 0.5f, true);
    UpdateEnv env;
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(member->m_happinessChanging, 0);
    OBL_CHECK_EQ(member->m_happinessRate, 0.0f);
    OBL_CHECK_EQ(member->m_happiness, 0.5004f);
    // 0.5009 and 0.5011 differ: the walk goes on
    auto going = Member(0.5009f, 0.5011f, 0.5f, true);
    staff::UpdateStaff(*going, env);
    OBL_CHECK_EQ(going->m_happinessChanging, 1);
    OBL_CHECK(going->m_happiness > 0.5009f);
    // without the flag nothing is reset even when they agree
    auto idle = Member(0.5f, 0.5f, 0.5f, false);
    staff::UpdateStaff(*idle, env);
    OBL_CHECK_EQ(idle->m_happinessRate, 0.5f);
    OBL_CHECK(idle->m_happiness > 0.5f);
}

OBL_TEST_CASE("staff_update.thousandths_are_truncated_toward_zero")
{
    // -0.0005 and +0.0005 both truncate to 0
    auto member = Member(-0.0005f, 0.0005f, 0.25f, true);
    UpdateEnv env;
    staff::UpdateStaff(*member, env);
    OBL_CHECK_EQ(member->m_happinessChanging, 0);
}

OBL_TEST_CASE("staff_update.happiness_is_clamped_to_zero_and_one")
{
    UpdateEnv env;
    auto up = Member(0.9995f, 1.0f, 100.0f, true);
    staff::UpdateStaff(*up, env);
    OBL_CHECK_EQ(up->m_happiness, 1.0f);
    auto down = Member(0.5f, 0.0f, -1000.0f, true);
    staff::UpdateStaff(*down, env);
    OBL_CHECK_EQ(down->m_happiness, 0.0f);
    OBL_CHECK(!std::signbit(down->m_happiness));
    // the sum -0 + 0 is +0 (and a negative zero is not below zero anyway)
    auto negativeZero = Member(-0.0f, 1.0f, 0.0f, false);
    staff::UpdateStaff(*negativeZero, env);
    OBL_CHECK_EQ(negativeZero->m_happiness, 0.0f);
    OBL_CHECK(!std::signbit(negativeZero->m_happiness));
}

OBL_TEST_CASE("staff_update.degenerate_frames_and_days_follow_the_double_results")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    UpdateEnv env;
    // a day shorter than 24 s: the hour is 0 s, the quotient an infinity: positive rate -> 1, negative -> 0, zero rate (inf * 0) -> NaN -> 0
    env.dayLength = 23;
    auto up = Member(0.5f, 1.0f, 0.5f, true);
    staff::UpdateStaff(*up, env);
    OBL_CHECK_EQ(up->m_happiness, 1.0f);
    auto down = Member(0.5f, 0.0f, -0.5f, true);
    staff::UpdateStaff(*down, env);
    OBL_CHECK_EQ(down->m_happiness, 0.0f);
    auto still = Member(0.5f, 0.5f, 0.0f, false);
    staff::UpdateStaff(*still, env);
    OBL_CHECK_EQ(still->m_happiness, 0.0f);
    // a negative day gives a negative hour: the walk reverses
    env.dayLength = -240;
    auto reversed = Member(0.5f, 1.0f, 0.5f, true);
    staff::UpdateStaff(*reversed, env);
    OBL_CHECK(std::fabs(reversed->m_happiness - 0.499f) < 1e-6f);
    // NaN in the happiness or the rate becomes 0; a NaN target only counts as 0 thousandths in the comparison
    env.dayLength = 240;
    auto nanHappiness = Member(nan, 1.0f, 0.5f, true);
    staff::UpdateStaff(*nanHappiness, env);
    OBL_CHECK_EQ(nanHappiness->m_happiness, 0.0f);
    auto nanRate = Member(0.5f, 1.0f, nan, true);
    staff::UpdateStaff(*nanRate, env);
    OBL_CHECK_EQ(nanRate->m_happiness, 0.0f);
    auto nanTarget = Member(0.0f, nan, 0.5f, true);
    staff::UpdateStaff(*nanTarget, env);
    OBL_CHECK_EQ(nanTarget->m_happinessChanging, 0); // FloatToIntWrap(NaN) = 0 = FloatToIntWrap(0.0)
    // a zero frame (the game paused) leaves the happiness
    env.frame = 0.0f;
    auto paused = Member(0.5f, 1.0f, 0.5f, true);
    staff::UpdateStaff(*paused, env);
    OBL_CHECK_EQ(paused->m_happiness, 0.5f);
}

OBL_TEST_CASE("staff_update.step_uses_double_precision_and_stores_a_float_once")
{
    auto member = Member(0.7f, 1.0f, 0.3f, true);
    UpdateEnv env;
    staff::UpdateStaff(*member, env);
    const double perHour = static_cast<double>(0.02f) / 10.0;
    const double expected = perHour * (static_cast<double>(0.3f) * 100.0) * static_cast<double>(0.01f) + static_cast<double>(0.7f);
    OBL_CHECK_EQ(member->m_happiness, static_cast<float>(expected));
}

namespace {

// The dispatcher's four calls; the log keeps their order, `staffOk` is the answer of Staff::Update.
struct DispatchEnv {
    std::string log;
    bool staffOk = true;
    int32_t startAfterScript = -1; // when >= 0 the script changes the shift start (the member under test) to this
    object::Builder* builder = nullptr;

    bool StaffUpdate()
    {
        add("Staff");
        return staffOk;
    }
    void FatalUpdate() { add("Fatal"); }
    void UpdateFeedback() { add("Feedback"); }
    void RunOnUpdate()
    {
        add("Script");
        if (builder && startAfterScript >= 0) builder->m_StartTime = startAfterScript;
    }
    void ReallocateBuilders() { add("Realloc"); }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

std::unique_ptr<object::Builder> MakeBuilder(int32_t start, int32_t last)
{
    auto builder = std::make_unique<object::Builder>();
    obl::unit::FillObject<object::Builder>(*builder);
    builder->m_StartTime = start;
    builder->m_LastStartTime = last;
    return builder;
}

} // namespace

OBL_TEST_CASE("staff_update.dispatcher_runs_the_base_the_feedback_and_the_script_in_order")
{
    DispatchEnv env;
    OBL_CHECK(staff::UpdateProfession(env));
    OBL_CHECK_EQ(env.log, std::string("Staff Feedback Script"));
}

OBL_TEST_CASE("staff_update.dispatcher_reports_a_failed_base_update_and_goes_on")
{
    DispatchEnv env;
    env.staffOk = false;
    OBL_CHECK(staff::UpdateProfession(env));
    OBL_CHECK_EQ(env.log, std::string("Staff Fatal Feedback Script"));
}

OBL_TEST_CASE("staff_update.builder_reallocates_only_when_the_shift_start_moved")
{
    DispatchEnv env;
    auto same = MakeBuilder(7, 7);
    staff::UpdateBuilder(*same, env);
    OBL_CHECK_EQ(env.log, std::string("Staff Feedback Script"));
    OBL_CHECK_EQ(same->m_LastStartTime, 7);

    env.log.clear();
    auto moved = MakeBuilder(9, 7);
    staff::UpdateBuilder(*moved, env);
    OBL_CHECK_EQ(env.log, std::string("Staff Feedback Script Realloc"));
    OBL_CHECK_EQ(moved->m_LastStartTime, 9);

    env.log.clear();
    auto unset = MakeBuilder(0, -1); // the constructor's "never seen" value
    staff::UpdateBuilder(*unset, env);
    OBL_CHECK_EQ(env.log, std::string("Staff Feedback Script Realloc"));
    OBL_CHECK_EQ(unset->m_LastStartTime, 0);
}

OBL_TEST_CASE("staff_update.builder_reads_the_shift_start_after_the_script")
{
    // the start is compared after the script ran, and the value after the call is the one kept
    DispatchEnv env;
    auto builder = MakeBuilder(7, 7);
    env.builder = builder.get();
    env.startAfterScript = 11;
    staff::UpdateBuilder(*builder, env);
    OBL_CHECK_EQ(env.log, std::string("Staff Feedback Script Realloc"));
    OBL_CHECK_EQ(builder->m_LastStartTime, 11);
}

OBL_TEST_CASE("staff_update.staff_script_is_the_word_at_0xf4")
{
    auto member = Member(0.5f, 0.5f, 0.0f, false);
    OBL_CHECK(object::StaffScript(*member) == nullptr);
    member->m_definition = reinterpret_cast<const obl::tables::GuestDef*>(static_cast<uintptr_t>(0x1234560));
    OBL_CHECK(reinterpret_cast<uintptr_t>(object::StaffScript(*member)) == 0x1234560u);
}
