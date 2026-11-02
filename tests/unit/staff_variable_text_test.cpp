#include "core/text16.h"
#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"

#include "game/objects/staff_variable_text.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

using namespace obl;
using namespace obl::world_data;
using obl::object::Builder;
using obl::object::Cleaner;
using obl::object::Lifeguard;
using obl::object::Mechanic;
using obl::object::Rep;
using obl::object::SecurityGuard;
using obl::script::ScriptProgram;
using namespace obl::tables;

namespace {

std::string Narrow(const Text16& text)
{
    std::string narrow;
    for (Char16 c : text) narrow += static_cast<char>(c);
    return narrow;
}

// The game as a log: lookups answer "<key>" in wide text, the printers write "<format>|<argument>" into the buffer, every call is listed in order.
struct Env {
    std::string log;
    int buildingMissing = 0;
    int beachClosed = 0;
    Text16 Wide(const std::string& text) { return Text16(text.begin(), text.end()); }
    const Char16* Lookup(const char* key)
    {
        add(std::string("Lookup(") + key + ")");
        static thread_local Text16 answer;
        answer = u"[" + Wide(key) + u"]";
        return answer.c_str();
    }
    void Print(Char16* out, const Char16* format)
    {
        add("Print(" + Narrow(format) + ")");
        obl::text16::Copy(out, format);
    }
    void PrintInt(Char16* out, const Char16* format, int value)
    {
        add("PrintInt(" + Narrow(format) + "," + std::to_string(value) + ")");
        obl::text16::Copy(out, (Text16(format) + u"|" + obl::text16::Decimal(value)).c_str());
    }
    void PrintReal(Char16* out, const Char16* format, double value)
    {
        add("PrintReal(" + Narrow(format) + "," + std::to_string(value) + ")");
        obl::text16::Copy(out, (Text16(format) + u"|" + obl::text16::Decimal(value)).c_str());
    }
    bool BuildingKey(int32_t id, std::string& key)
    {
        add("BuildingKey(" + std::to_string(id) + ")");
        if (buildingMissing) return false;
        key = "KEY" + std::to_string(id);
        return true;
    }
    bool BuildingName(int32_t id, Char16* out)
    {
        add("BuildingName(" + std::to_string(id) + ")");
        if (buildingMissing) return false;
        obl::text16::Copy(out, u"building");
        return true;
    }
    bool BeachOpen(int32_t id, bool& open)
    {
        add("BeachOpen(" + std::to_string(id) + ")");
        if (buildingMissing) return false;
        open = !beachClosed;
        return true;
    }
    void AskFeedback(uint32_t flags) { add("Feedback(" + std::to_string(flags) + ")"); }
    void Fatal(const char* key) { add(std::string("Fatal(") + key + ")"); }
    void add(const std::string& call) { log += (log.empty() ? "" : " ") + call; }
};

template<class T> std::unique_ptr<T> Member()
{
    auto member = std::make_unique<T>();
    obl::unit::FillObject<T>(*member);
    return member;
}

struct Out {
    Char16 text[256];
    Out() { std::char_traits<obl::Char16>::assign(text, 255, u'#'), text[255] = 0; }
    std::string Get() const { return Narrow(text); }
};

const Char16 kNameFormat[] = u"Name %d";

} // namespace

OBL_TEST_CASE("staff_variable_text.base_activity_follows_the_state_switch")
{
    struct Case {
        int32_t state;
        std::string log;
        std::string text;
    };
    const Case cases[] = {{0, "", ""}, {1, "Lookup(STARTING_WORK)", "[STARTING_WORK]"}, {6, "Lookup(SACKED)", "[SACKED]"},
            {0x4c, "Lookup(SACKED)", "[SACKED]"}, {0x4d, "Lookup(REQUIRES_ATTENTION)", "[REQUIRES_ATTENTION]"},
            {0x83, "Lookup(BEING_DIRECTED)", "[BEING_DIRECTED]"}, {0x84, "Lookup(BEING_DIRECTED)", "[BEING_DIRECTED]"},
            {2, "", "Not done yet"}, {0x85, "", "Not done yet"}, {-1, "", "Not done yet"}};
    for (const Case& c : cases) {
        auto member = Member<object::Staff>();
        member->m_state = c.state;
        Env env;
        Out out;
        staff::BaseVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, c.log);
        OBL_CHECK_EQ(out.Get(), c.text);
    }
}

OBL_TEST_CASE("staff_variable_text.base_numbers_and_hours")
{
    auto member = Member<object::Staff>();
    member->m_Wage = 90;
    member->m_happiness = 0.755f;
    member->m_StartTime = 8;
    member->m_EndTime = 17;
    member->m_HiredOn = 12;
    const struct {
        const char* key;
        const char* log;
        const char* text;
    } cases[] = {
            {"STAFF_WAGES", "PrintInt(%d,90)", "%d|90"},
            {"STAFF_WAGES_ANY", "PrintInt(%d,90)", "%d|90"},
            {"STAFF_DAY_WAGES_$", "PrintInt($%d,900)", "$%d|900"},
            {"STAFF_HAPPY_PERCENT", "PrintInt(%d%%,75)", "%d%%|75"},
            {"STAFF_HIRED_ON", "Lookup(STAFF_DAY_TEXT) PrintInt([STAFF_DAY_TEXT],12)", "[STAFF_DAY_TEXT]|12"},
            {"STAFF_START_HOUR", "PrintInt(%d,8)", "%d|8"},
            {"STAFF_END_HOUR", "PrintInt(%d,17)", "%d|17"},
            {"STAFF_HOUR_SEP", "Lookup(STAFFB_HOUR_SEP) Print([STAFFB_HOUR_SEP])", "[STAFFB_HOUR_SEP]"},
    };
    for (const auto& c : cases) {
        Env env;
        Out out;
        staff::BaseVariableText(*member, c.key, out.text, env);
        OBL_CHECK_EQ(env.log, std::string(c.log));
        OBL_CHECK_EQ(out.Get(), std::string(c.text));
    }
}

OBL_TEST_CASE("staff_variable_text.base_wage_times_ten_wraps_and_happiness_uses_truncation")
{
    auto member = Member<object::Staff>();
    member->m_Wage = 0x30000000;
    Env env;
    Out out;
    staff::BaseVariableText(*member, "STAFF_DAY_WAGES_$", out.text, env);
    OBL_CHECK_EQ(env.log, std::string("PrintInt($%d,") + std::to_string(static_cast<int32_t>(0x30000000u * 10u)) + ")");
    member->m_happiness = 1.0e30f; // beyond 64 bits: the integer indefinite, i.e. 0
    Env env2;
    staff::BaseVariableText(*member, "STAFF_HAPPY_PERCENT", out.text, env2);
    OBL_CHECK_EQ(env2.log, std::string("PrintInt(%d%%,0)"));
}

OBL_TEST_CASE("staff_variable_text.base_home_thoughts_and_dismissal")
{
    auto member = Member<object::Staff>();
    StaffDef definition{};
    definition.m_RecommendedWage = 40;
    member->m_staffDefinition = &definition;
    member->m_HomeID = 7;
    for (int i = 0; i < 3; ++i) {
        member->m_Thoughts[i][0] = static_cast<Char16>(u'a' + i);
        member->m_Thoughts[i][1] = 0;
    }
    {
        Env env;
        Out out;
        staff::BaseVariableText(*member, "STAFF_HOME", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("BuildingKey(7) Lookup(BUILDING_NAME_KEY7)"));
        OBL_CHECK_EQ(out.Get(), std::string("[BUILDING_NAME_KEY7]"));
    }
    {
        Env env;
        env.buildingMissing = 1;
        Out out;
        staff::BaseVariableText(*member, "STAFF_HOME", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("BuildingKey(7)"));
        OBL_CHECK_EQ(out.Get().substr(0, 3), std::string("###"));
    }
    const char* const messages[] = {"STAFF_FEEDBACK_MESS0", "STAFF_FEEDBACK_MESS1X", "STAFF_FEEDBACK_MESS2"};
    for (int i = 0; i < 3; ++i) {
        Env env;
        Out out;
        staff::BaseVariableText(*member, messages[i], out.text, env);
        OBL_CHECK_EQ(env.log, std::string());
        OBL_CHECK_EQ(out.Get(), std::string(1, static_cast<char>('a' + i)));
    }
    Env env;
    Out out;
    staff::BaseVariableText(*member, "STAFF_DISMISS_CONFIRM", out.text, env);
    OBL_CHECK_EQ(env.log, std::string("Lookup(STAFFB_DISMISS_CONFIRM) PrintInt([STAFFB_DISMISS_CONFIRM],400)"));
}

OBL_TEST_CASE("staff_variable_text.base_unrecognized_key_is_fatal")
{
    auto member = Member<object::Staff>();
    for (const char* key : {"STAFF_WAGE", "", "staff_name", "STAFF_FEEDBACK_MESS", "STAFF_JOB_TITLE"}) {
        Env env;
        Out out;
        staff::BaseVariableText(*member, key, out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Fatal(") + key + ")");
        OBL_CHECK_EQ(out.Get().substr(0, 3), std::string("###"));
    }
}

OBL_TEST_CASE("staff_variable_text.builder_states_and_own_keywords")
{
    auto member = Member<Builder>();
    member->m_DestObjectID = 33;
    member->m_script = reinterpret_cast<obl::script::ScriptProgram*>(const_cast<Char16*>(kNameFormat));
    const char* const states[] = {"BUILDER_RESTING", "BUILDER_HEADING_HOME", "BUILDER_GOING_TO_BUILD", "BUILDER_GOING_TO_DEMOLISH",
            "BUILDER_BUILDING", "BUILDER_DEMOLISHING", "BUILDER_RESTING", "BUILDER_RESTING", "BUILDER_IDLE"};
    for (int i = 0; i < 9; ++i) {
        member->m_state = 0x5f + i;
        Env env;
        Out out;
        staff::BuilderVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Lookup(") + states[i] + ")");
        OBL_CHECK_EQ(out.Get(), std::string("[") + states[i] + "]");
    }
    // Outside its own range the base class answers.
    member->m_state = 0x68;
    {
        Env env;
        Out out;
        staff::BuilderVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(out.Get(), std::string("Not done yet"));
    }
    member->m_state = 0x5e;
    {
        Env env;
        Out out;
        staff::BuilderVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(out.Get(), std::string("Not done yet"));
    }
    member->m_state = 1;
    {
        Env env;
        Out out;
        staff::BuilderVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Lookup(STARTING_WORK)"));
    }
    {
        Env env;
        Out out;
        staff::BuilderVariableText(*member, "BUILDER_CURRENT_BUILD", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("BuildingName(33)"));
        OBL_CHECK_EQ(out.Get(), std::string("building"));
    }
    {
        Env env;
        Out out;
        staff::BuilderVariableText(*member, "STAFF_NAME", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Print(Name %d)"));
    }
    {
        Env env;
        Out out;
        staff::BuilderVariableText(*member, "STAFF_JOB_TITLE", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Lookup(STAFFB_TITLE_BUILDER) Print([STAFFB_TITLE_BUILDER])"));
    }
    {
        Env env;
        Out out;
        member->m_Wage = 9;
        staff::BuilderVariableText(*member, "STAFF_WAGES", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("PrintInt(%d,9)"));
    }
}

OBL_TEST_CASE("staff_variable_text.rep_asks_for_feedback_first_and_answers_its_keywords")
{
    auto member = Member<Rep>();
    member->m_CurrentFeedback = 0x40;
    member->m_GuestsTalkedToToday = 5;
    const float boost = 12.9f;
    std::memcpy(&member->m_happinessBoostToday, &boost, sizeof(boost));
    member->m_timeWithGuests = 2.5f;
    member->m_script = reinterpret_cast<obl::script::ScriptProgram*>(const_cast<Char16*>(kNameFormat));
    const char* const states[] = {
            "REPRESENTATIVE_RESTING", "REPRESENTATIVE_PATROLLING", "REPRESENTATIVE_TALKING", nullptr, "REPRESENTATIVE_IDLE"};
    for (int i = 0; i < 5; ++i) {
        member->m_state = 7 + i;
        Env env;
        Out out;
        staff::RepVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        if (states[i]) {
            OBL_CHECK_EQ(env.log, std::string("Feedback(64) Print() Lookup(") + states[i] + ") Print([" + states[i] + "])");
            OBL_CHECK_EQ(out.Get(), std::string("[") + states[i] + "]");
        } else {
            OBL_CHECK_EQ(env.log, std::string("Feedback(64) Print() Print()"));
            OBL_CHECK_EQ(out.Get(), std::string());
        }
    }
    member->m_state = 12;
    {
        Env env;
        Out out;
        staff::RepVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Feedback(64) Print()"));
        OBL_CHECK_EQ(out.Get(), std::string("Not done yet"));
    }
    const struct {
        const char* key;
        const char* log;
    } cases[] = {
            {"STAFF_NAME", "Print(Name %d)"},
            {"GUESTS_TODAY", "PrintInt(%d,5)"},
            {"HAPPINESS_BOOST_TODAY", "PrintInt(%d%%,12)"},
            {"REP_GUEST_TIME", "PrintReal(%f,2.500000)"},
            {"STAFF_JOB_TITLE", "Lookup(STAFFB_TITLE_REP) Print([STAFFB_TITLE_REP])"},
            {"STAFF_WAGES", "PrintInt(%d,0)"},
    };
    for (const auto& c : cases) {
        Env env;
        Out out;
        staff::RepVariableText(*member, c.key, out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Feedback(64) Print() ") + c.log);
    }
}

OBL_TEST_CASE("staff_variable_text.cleaner_states_counters_and_title")
{
    auto member = Member<Cleaner>();
    member->m_CurrentFeedback = 8;
    member->m_script = reinterpret_cast<obl::script::ScriptProgram*>(const_cast<Char16*>(kNameFormat));
    const char* const states[] = {"CLEANER_CHOOSE_TASK", "CLEANER_GOING_TO_REST", "CLEANER_GOING_TO_REST", "CLEANER_RESTING",
            "CLEANER_RESTING", "CLEANER_GO_CLEAN_LITTER", "CLEANER_CLEANING_LITTER", "CLEANER_GO_CLEAN_TOILET", "CLEANER_GO_CLEAN_BIN",
            "CLEANER_CLEANING_BIN", "CLEANER_GO_CLEAN_TOILET", "CLEANER_CLEANING_TOILET", "CLEANER_CLEANING_TOILET", "CLEANER_IDLE"};
    for (int i = 0; i < 14; ++i) {
        member->m_state = 0x68 + i;
        Env env;
        Out out;
        staff::CleanerVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Print() Feedback(8) Lookup(") + states[i] + ")");
        OBL_CHECK_EQ(out.Get(), std::string("[") + states[i] + "]");
    }
    for (int32_t state : {0x67, 0x76, 2, -1}) {
        member->m_state = state;
        Env env;
        Out out;
        staff::CleanerVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Print() Feedback(8)"));
        OBL_CHECK_EQ(out.Get(), std::string("Not done yet"));
    }
    member->m_cleanToilets = 7, member->m_cleanLitter = 8, member->m_cleanBins = 9;
    member->m_ToiletsToday = 11, member->m_BinsToday = 12, member->m_LitterToday = 13;
    const struct {
        const char* key;
        const char* log;
    } cases[] = {
            {"CLEANER_TOILETS", "PrintInt(%d,7)"},
            {"CLEANER_BINS", "PrintInt(%d,9)"},
            {"CLEANER_LITTER", "PrintInt(%d,8)"},
            {"STAFF_NAME", "Print(Name %d)"},
            {"TOILETS_CLEANED", "PrintInt(%d,11)"},
            {"BINS_EMPTIED", "PrintInt(%d,12)"},
            {"LITTER_SWEPT", "PrintInt(%d,13)"},
            {"STAFF_JOB_TITLE", "Lookup(STAFFB_TITLE_CLEANER) Print([STAFFB_TITLE_CLEANER])"},
            {"STAFF_WAGES", "PrintInt(%d,0)"},
    };
    for (const auto& c : cases) {
        Env env;
        Out out;
        staff::CleanerVariableText(*member, c.key, out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Print() Feedback(8) ") + c.log);
    }
}

OBL_TEST_CASE("staff_variable_text.lifeguard_states_rescues_and_beach")
{
    auto member = Member<Lifeguard>();
    member->m_CurrentFeedback = 2;
    member->m_NumSaves = 3;
    member->m_NumDrowned = 4;
    member->m_HomeID = 5;
    member->m_script = reinterpret_cast<obl::script::ScriptProgram*>(const_cast<Char16*>(kNameFormat));
    const char* const states[] = {"LIFEGUARD_PATROL", "LIFEGUARD_RESTING", "LIFEGUARD_RESCUE", "LIFEGUARD_GOTO_TOWER",
            "LIFEGUARD_LEAVING_TOWER", "LIFEGUARD_ON_TOWER", "LIFEGUARD_TALKING", nullptr, "LIFEGUARD_TELLOFF", "LIFEGUARD_IDLE"};
    for (int i = 0; i < 10; ++i) {
        member->m_state = 0x4e + i;
        Env env;
        Out out;
        staff::LifeguardVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        if (states[i]) {
            OBL_CHECK_EQ(env.log, std::string("Print() Feedback(2) Lookup(") + states[i] + ") Print([" + states[i] + "])");
            OBL_CHECK_EQ(out.Get(), std::string("[") + states[i] + "]");
        } else {
            OBL_CHECK_EQ(env.log, std::string("Print() Feedback(2) Print()"));
            OBL_CHECK_EQ(out.Get(), std::string());
        }
    }
    member->m_state = 0x4b; // the base class's SACKED and attention states are 0x4c and 0x4d
    {
        Env env;
        Out out;
        staff::LifeguardVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(out.Get(), std::string("Not done yet"));
    }
    const struct {
        const char* key;
        const char* log;
    } cases[] = {
            {"STAFF_NAME", "Print(Name %d)"},
            {"RESCUE_ATTEMPTS_TODAY", "PrintInt(%d,7)"},
            {"RESCUES_TODAY", "PrintInt(%d,3)"},
            {"STAFF_JOB_TITLE", "Lookup(STAFFB_TITLE_LIFEGUARD) Print([STAFFB_TITLE_LIFEGUARD])"},
            {"STAFF_BEACH_OPEN", "BeachOpen(5) PrintInt(%d,1)"},
            {"STAFF_BEACH_OPEN_STATUS_", "PrintInt(%d,1)"},
            {"STAFF_BEACH_OPEN_STATUS_ANY", "PrintInt(%d,1)"},
            {"STAFF_WAGES", "PrintInt(%d,0)"},
    };
    for (const auto& c : cases) {
        Env env;
        Out out;
        staff::LifeguardVariableText(*member, c.key, out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Print() Feedback(2) ") + c.log);
    }
    Env env;
    env.beachClosed = 1;
    Out out;
    staff::LifeguardVariableText(*member, "STAFF_BEACH_OPEN", out.text, env);
    OBL_CHECK_EQ(out.Get(), std::string("%d|0"));
    env.buildingMissing = 1;
    Out missing;
    staff::LifeguardVariableText(*member, "STAFF_BEACH_OPEN", missing.text, env);
    OBL_CHECK_EQ(missing.Get(), std::string()); // cleared first, then left alone
}

OBL_TEST_CASE("staff_variable_text.mechanic_asks_feedback_before_clearing_and_reads_the_job_mask")
{
    auto member = Member<Mechanic>();
    member->m_CurrentFeedback = 16;
    member->m_JobMask = 0xfffffffa; // bit 0 off, bits 1 and 2 on
    member->m_inspectionsToday = 1, member->m_repairsToday = 2, member->m_vandalsToday = 3, member->m_wateredToday = 4;
    member->m_script = reinterpret_cast<obl::script::ScriptProgram*>(const_cast<Char16*>(kNameFormat));
    const char* const states[] = {"MECHANIC_REPAIR", "MECHANIC_RESTING", "MECHANIC_HEAD_HOME", "MECHANIC_CHOOSE_JOB", "MECHANIC_GO_REST",
            "MECHANIC_GO_REPAIR", "MECHANIC_IDLE"};
    for (int i = 0; i < 7; ++i) {
        member->m_state = 0x58 + i;
        Env env;
        Out out;
        staff::MechanicVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Feedback(16) Print() Lookup(") + states[i] + ")");
        OBL_CHECK_EQ(out.Get(), std::string("[") + states[i] + "]");
    }
    member->m_state = 0x5f;
    {
        Env env;
        Out out;
        staff::MechanicVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(out.Get(), std::string("Not done yet"));
    }
    const struct {
        const char* key;
        const char* log;
    } cases[] = {
            {"MECHANIC_VANDALS", "PrintInt(%d,1)"},
            {"MECHANIC_REPAIR", "PrintInt(%d,0)"},
            {"MECHANIC_WATER", "PrintInt(%d,0)"},
            {"STAFF_NAME", "Print(Name %d)"},
            {"INSPECT_TODAY", "PrintInt(%d,1)"},
            {"REPAIRS_TODAY", "PrintInt(%d,2)"},
            {"VANDAL_REPAIRS_TODAY", "PrintInt(%d,3)"},
            {"WATERED_TODAY", "PrintInt(%d,4)"},
            {"STAFF_JOB_TITLE", "Lookup(STAFFB_TITLE_MECHANIC) Print([STAFFB_TITLE_MECHANIC])"},
    };
    for (const auto& c : cases) {
        Env env;
        Out out;
        staff::MechanicVariableText(*member, c.key, out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Feedback(16) Print() ") + c.log);
    }
    member->m_JobMask = 5; // repairs and watering
    Env env;
    Out out;
    staff::MechanicVariableText(*member, "MECHANIC_WATER", out.text, env);
    OBL_CHECK_EQ(env.log, std::string("Feedback(16) Print() PrintInt(%d,1)"));
}

OBL_TEST_CASE("staff_variable_text.security_guard_states_fines_and_harshness")
{
    auto member = Member<SecurityGuard>();
    member->m_CurrentFeedback = 32;
    member->m_numCautioned = 6;
    member->m_numJailed = 7;
    member->m_fineTotalToday = 123.9f;
    member->m_minorFineAmount = 10.0f;
    member->m_seriousFineAmount = 50.5f;
    member->m_tolerance = 0.5f;
    member->m_script = reinterpret_cast<obl::script::ScriptProgram*>(const_cast<Char16*>(kNameFormat));
    const char* const states[] = {"SECURITYGUARD_WAVE_HELLO", "SECURITYGUARD_CHOOSE_TASK", "SECURITYGUARD_PATROL", "SECURITYGUARD_GO_REST",
            "SECURITYGUARD_CHOOSE_ACTION", "SECURITYGUARD_RESTING", "SECURITYGUARD_SUSPICIOUS", "SECURITYGUARD_REPRIMAND",
            "SECURITYGUARD_DETAIN", "SECURITYGUARD_ESCORT", "SECURITYGUARD_GO_REST", "SECURITYGUARD_PATROL", "SECURITYGUARD_IDLE"};
    for (int i = 0; i < 13; ++i) {
        member->m_state = 0x76 + i;
        Env env;
        Out out;
        staff::SecurityGuardVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Print() Feedback(32) Lookup(") + states[i] + ") Print([" + states[i] + "])");
    }
    member->m_state = 0x85;
    {
        Env env;
        Out out;
        staff::SecurityGuardVariableText(*member, "STAFF_ACTIVITY", out.text, env);
        OBL_CHECK_EQ(out.Get(), std::string("Not done yet"));
    }
    const struct {
        const char* key;
        const char* log;
    } cases[] = {
            {"STAFF_NAME", "Print(Name %d)"},
            {"GUARD_CAUTIONED_TODAY", "PrintInt(%d,6)"},
            {"GUARD_DETAINED_TODAY", "PrintInt(%d,7)"},
            {"GUARD_FINES_TODAY_$", "PrintInt($%d,123)"},
            {"SECURITY_HARSHNESS", "PrintReal(%f,0.500000)"},
            {"GUARD_FINE_MINOR_$", "PrintInt($%d,10)"},
            {"GUARD_FINE_MINOR_$ANY", "PrintInt($%d,10)"},
            {"GUARD_FINE_SERIOUS_$", "PrintInt($%d,50)"},
            {"GUARD_FINE_SERIOUS_$ANY", "PrintInt($%d,50)"},
            {"STAFF_JOB_TITLE", "Lookup(STAFFB_TITLE_SECURITY) Print([STAFFB_TITLE_SECURITY])"},
            {"STAFF_WAGES", "PrintInt(%d,0)"},
    };
    for (const auto& c : cases) {
        Env env;
        Out out;
        staff::SecurityGuardVariableText(*member, c.key, out.text, env);
        OBL_CHECK_EQ(env.log, std::string("Print() Feedback(32) ") + c.log);
    }
}
