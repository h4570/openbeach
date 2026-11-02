// Text of GuiVariable keys (src/game/ui/gui_variable.h): formatting, colour and which keys are handled.

#include "core/text16.h"
#include "unit_test.h"

#include "game/ui/gui_variable.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"

#include <cmath>
#include <cstdarg>
#include <cwchar>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace obl::gui_variable;

namespace {

#ifdef _WIN32
const obl::Char16* DefaultFormat(const obl::Char16* format, obl::Text16&)
{
    return format;
}
#else
// msvcrt reads `%s` in a wide format as a wide string; glibc reads a narrow one and spells the wide one `%ls`.
const obl::Char16* DefaultFormat(const obl::Char16* format, obl::Text16& storage)
{
    storage.clear();
    for (const obl::Char16* p = format; *p; ++p) {
        storage += *p;
        if (*p != u'%') continue;
        if (p[1] == u'%') {
            storage += *++p;
            continue;
        }
        while (p[1] && obl::text16::Find(u"-+ #0123456789.*", p[1])) storage += *++p;
        if (p[1] == u's') storage += u'l';
    }
    return storage.c_str();
}
#endif

int __cdecl TestSwprintf(obl::Char16* out, const obl::Char16* format, ...)
{
    obl::Text16 storage;
    va_list args;
    va_start(args, format);
    const int written = obl::text16::FormatV(out, kTextCapacity, DefaultFormat(format, storage), args);
    va_end(args);
    return written;
}

class FakeEnv final : public Env {
public:
    float cash = 0;
    int guests = 0, day = 0;
    float time = 0, minDistance = 0, maxDistance = 0;
    float powerOutput = 0, powerDemand = 0, powerUse = 0;
    int weatherColor = 0, litter = 0, sceneryPlain = 0, zeroGender = 0, nonZeroGender = 0;
    int classCounts[16] = {};
    int flows[6][2] = {};
    float money[15] = {};
    mutable int textLookups = 0;

    // Scenario panel: text-table keys, objective flags, World counters and the scenario the menu picked.
    const char* descriptionKey = "DESC_KEY";
    const char* objectivesKey = "OBJS_KEY";
    const char* titleKey = "TITLE_KEY";
    uint32_t objectiveCount = 0;
    const char* objectiveKeys[4] = {"OBJ_A", "OBJ_B", "OBJ_C", "OBJ_D"};
    uint32_t objectiveStatus[4] = {};
    int counters[10] = {};
    bool sandbox = false;
    int scenario = 0;
    bool completed[12] = {}, always[12] = {};
    int lodgings = 0;

    // Ferry panel: what the schedule answers (a time <= 0 means no boat) and the day it picked for a later boat.
    double nextToday = -1, nextLater = -1;
    uint32_t nextDay = 0;
    std::vector<double> todayBoats;
    bool wharf = false;

    const char* islandKeys[5] = {"ISL_TITLE", "ISL_AREA", "ISL_WEATHER", "ISL_TEMP", "ISL_HAZARDS"};
    const char* scriptTextKey = "SCRIPT_KEY";
    const char* messageTextKey = "MESSAGE_KEY";
    double version = 1.100000023841858; // (double)1.1f
    const char* filterHeading = "HEADING_KEY"; // null: no active filter
    int browserCount = 0;

    // Staff browser: the category, workers by object class (on the payroll), idle home buildings by key and the per-home limit.
    int staffCategory = 0, staffAll = 0;
    int payroll[16] = {};
    std::map<std::string, int> idleHomes;
    int maxStaffPerHome = 0;
    mutable std::string maxStaffKey;

    // Variable provider: absent by default; when present it knows the keys of `providerTexts` and writes nothing for others.
    bool hasProvider = false;
    std::map<std::string, obl::Text16> providerTexts;
    mutable std::vector<std::string> providerAsked;
    mutable std::vector<int> nextCalls;
    mutable bool wharfAsked = false;

    // Build panel: two buildings (0 "BAR" with tree "NONE", 1 "DISCO" needing "BAR"), two scenery pieces.
    struct Building {
        const char* key;
        int cost, power;
        const char* tree;
        const char* hours;
        int slots;
        float appeal[3][2];
        bool allowed;
    };
    Building buildings[2] = {
            {"BAR", 1500, 20, "NONE", "ALLHOURS", 1, {{1.0f, 0.0f}}, true}, {"DISCO", 9000, -35, "BAR", "NIGHT", 0, {}, true}};
    struct Scenery {
        const char* key;
        int cost;
    };
    Scenery scenery[2] = {{"bin", 40}, {"Palm_Tree", 250}};
    int selectedBuilding = 0, selectedScenery = 0;
    bool idleBuilding = true;
    mutable std::string lastIdleKey;

    float Cash() const override { return cash; }
    int NumGuests() const override { return guests; }
    int GameDay() const override { return day; }
    float GameTime() const override { return time; }
    float MinDistance() const override { return minDistance; }
    float MaxDistance() const override { return maxDistance; }
    const obl::Char16* Text(const char* key) const override
    {
        ++textLookups;
        const std::string k = key;
        if (k == "CASH_TITLE") return u"Cash: $%.0f";
        if (k == "GUESTS_TITLE") return u"Guests (%d)";
        if (k == "DAY_TITLE") return u"Day %d";
        if (k == "IS_LITTER_ITEMS") return u"%d litter items";
        if (k == "IS_GUEST_COUNT") return u"%d guests: %d women, %d men";
        if (k == "IS_SCENERY_OBJECTS") return u"%d scenery objects";
        if (k == "FP_GUEST_FORMAT") return u"%d (%d/%d)";
        if (k == "FP_UNHAPPY_FORMAT") return u"%d (%d/%d) %d%%";
        if (k == "FP_ARRIVE_FORMAT") return u"%d:%02d on day %d";
        if (k == "ISL_TITLE") return u"Sunny Cove";
        if (k == "ISL_AREA") return u"Area: 40%% of the bay"; // the text is the format string, so %% prints one percent sign
        if (k == "ISL_WEATHER") return u"Mild";
        if (k == "ISL_TEMP") return u"25C";
        if (k == "ISL_HAZARDS") return u"Sharks";
        if (k == "SCRIPT_KEY") return u"Welcome 100% %d";
        if (k == "MESSAGE_KEY") return u"Message text";
        if (k == "HEADING_KEY") return u"Happiness";
        if (k == "OB_NUM_GUESTS") return u"%d guests";
        if (k == "OB_NUM_BUILDINGS") return u"%d buildings";
        if (k == "MAINMENU_VERSION") return u"Version %.1f";
        if (k == "FP_SUMMARY_FERRY") return u"Ferry summary";
        if (k == "FP_SUMMARY_BUS") return u"Bus summary";
        if (k == "FP_NEXT_FERRY") return u"Next ferry";
        if (k == "FP_NEXT_BUS") return u"Next bus";
        if (k == "FP_SCHEDULE_FERRY") return u"Ferry schedule";
        if (k == "FP_SCHEDULE_BUS") return u"Bus schedule";
        static const std::map<std::string, const obl::Char16*> kTexts = {{"BUILD_DESC_BAR", u"A bar"}, {"BUILD_DESC_DISCO", u"A disco"},
                {"BUILD_SNAME_BAR", u"Beach Bar"}, {"BUILD_SNAME_DISCO", u"Disco"}, {"BUILDING_NAME_DISCO", u"Disco Inferno"},
                {"BI_HOURS_ALLDAY", u"All day"}, {"BI_HOURS_MORNING", u"Mornings"}, {"BI_HOURS_DAY", u"Daytime"},
                {"BI_HOURS_EVENING", u"Evenings"}, {"BI_HOURS_NIGHT", u"Nights"}, {"SP_DESC_BIN", u"A bin"},
                {"SP_DESC_PALM_TREE", u"A palm tree"}, {"BUILDMENU_APPEAL", u"Appeals"}, {"OBJ_SCENARIO_DESCRIPTION", u"Goal"},
                {"OBJ_SCENARIO_OBJECTIVES", u"Tasks"}, {"OBJ_SCENARIO_PROGRESS", u"Progress"}, {"DESC_KEY", u"Build a resort"},
                {"OBJS_KEY", u"Make money"}, {"TITLE_KEY", u"Beach Basics"}, {"OBJ_A", u"Objective A"}, {"OBJ_B", u"Objective B"},
                {"OBJ_C", u"Objective C"}, {"OBJ_D", u"Objective D"}, {"MAINSS_SCENARIO3_TITLE", u"Scenario three"},
                {"MAINSS_SANDBOX3_TITLE", u"Sandbox three"}, {"MAINSS_SCENARIO3_DESC", u"About scenario three"},
                {"MAINSS_SANDBOX3_DESC", u"About sandbox three"}, {"MAINSS_SCENARIO_COMPLETE", u"Complete"},
                {"MAINSS_SCENARIO_ALWAYS", u"Always open"}, {"MAINSS_SCENARIO_NOTCOMPLETED", u"Not completed"},
                {"PM_BUILD_TREE_FORMAT", u"%d of %d built"}, {"PM_5_UNIQUE_FORMAT", u"%d unique"}, {"STAFFB_STAFF_ALL", u"%d staff"},
                {"STAFFB_HIRED_MAXIMUM", u"%d %s (max %d)"}, {"STAFFB_TITLE_CLEANER", u"cleaner"}, {"STAFFB_FILTER_CLEANERS", u"cleaners"},
                {"STAFFB_TITLE_BUILDER", u"builder"}, {"STAFFB_FILTER_BUILDERS", u"builders"}, {"STAFFB_TITLE_MECHANIC", u"mechanic"},
                {"STAFFB_FILTER_MECHANICS", u"mechanics"}, {"STAFFB_TITLE_REP", u"rep"}, {"STAFFB_FILTER_REP", u"reps"},
                {"STAFFB_TITLE_LIFEGUARD", u"lifeguard"}, {"STAFFB_FILTER_LIFEGUARD", u"lifeguards"}, {"STAFFB_TITLE_SECURITY", u"guard"},
                {"STAFFB_FILTER_SECURITY", u"guards"}};
        const auto found = kTexts.find(k);
        if (found != kTexts.end()) return found->second;
        return u"";
    }
    int WholeHour(float t) const override { return static_cast<int>(std::floor(t)); }
    int QuarterMinutes(float t) const override
    {
        const float m = (t - std::floor(t)) * 60.0f;
        return m >= 45.0f ? 45 : m >= 30.0f ? 30 : m >= 15.0f ? 15 : 0;
    }
    Swprintf Format() const override { return &TestSwprintf; }
    double NextBoatTime(int todayOnly, uint32_t* outDay) const override
    {
        nextCalls.push_back(todayOnly);
        if (todayOnly) return nextToday;
        *outDay = nextDay;
        return nextLater;
    }
    const char* IslandKey(IslandText text) const override { return islandKeys[static_cast<int>(text)]; }
    const char* ScriptTextKey() const override { return scriptTextKey; }
    const char* MessageTextKey() const override { return messageTextKey; }
    double GameVersion() const override { return version; }
    const char* FilterHeadingKey() const override { return filterHeading; }
    int BrowserCount() const override { return browserCount; }
    int StaffCategory() const override { return staffCategory; }
    int StaffCountAll() const override { return staffAll; }
    int StaffOnPayroll(int objectClass) const override { return payroll[objectClass]; }
    int IdleBuildingCount(const char* key) const override
    {
        const auto found = idleHomes.find(key);
        return found != idleHomes.end() ? found->second : 0;
    }
    int BuildingMaxStaff(int building) const override { return building == 7 ? maxStaffPerHome : -1; }
    bool ProviderText(const char* key, obl::Char16* out) const override
    {
        providerAsked.push_back(key);
        if (!hasProvider) return false;
        const auto found = providerTexts.find(key);
        const obl::Text16 text = found != providerTexts.end() ? found->second : obl::Text16();
        obl::text16::Copy(out, text.c_str());
        return true;
    }
    bool ObjectExists(const char* key) const override
    {
        wharfAsked = std::string(key) == "WHARF";
        return wharf;
    }
    double BoatTimeToday(int index) const override { return index < static_cast<int>(todayBoats.size()) ? todayBoats[index] : -1.0; }

    float PowerOutput() const override { return powerOutput; }
    float PowerDemand() const override { return powerDemand; }
    float PowerUse() const override { return powerUse; }
    int WeatherColor() const override { return weatherColor; }
    int LitterCount() const override { return litter; }
    int ObjectCount(int objectClass) const override { return classCounts[objectClass]; }
    int SceneryNonLitterCount() const override { return sceneryPlain; }
    int GuestGenderCount(bool nonZero) const override { return nonZero ? nonZeroGender : zeroGender; }
    int GuestFlow(GuestFlowField field, int type) const override { return flows[static_cast<int>(field)][type]; }
    float Money(MoneyField field) const override { return money[static_cast<int>(field)]; }

    int SelectedBuilding() const override { return selectedBuilding; }
    int SelectedScenery() const override { return selectedScenery; }
    const char* BuildingKey(int b) const override { return buildings[b].key; }
    int BuildingCost(int b) const override { return buildings[b].cost; }
    int BuildingPower(int b) const override { return buildings[b].power; }
    const char* BuildingTreeKey(int b) const override { return buildings[b].tree; }
    const char* BuildingHoursKey(int b) const override { return buildings[b].hours; }
    int BuildingIdByKey(const char* key) const override
    {
        const std::string k = key;
        if (k == "CLEANERSHACK" || k == "BUILDERYARD" || k == "HANDYMAN" || k == "REP" || k == "BAYWATCH" || k == "SECURITY_POST") {
            maxStaffKey = k;
            return 7;
        }
        return k == "BAR" ? 0 : 1;
    }
    bool BuildingAllowed(int b) const override { return buildings[b].allowed; }
    bool HasIdleBuilding(const char* key) const override
    {
        lastIdleKey = key;
        return idleBuilding;
    }
    int BuildingCommodityCount(int b) const override { return buildings[b].slots; }
    float BuildingAppeal(int b, int slot, bool female) const override { return buildings[b].appeal[slot][female ? 1 : 0]; }
    const char* SceneryKey(int s) const override { return scenery[s].key; }
    int SceneryCost(int s) const override { return scenery[s].cost; }

    const char* ScenarioDescriptionKey() const override { return descriptionKey; }
    const char* ScenarioObjectivesKey() const override { return objectivesKey; }
    const char* ScenarioTitleKey() const override { return titleKey; }
    uint32_t ObjectiveCount() const override { return objectiveCount; }
    const char* ObjectiveKey(uint32_t index) const override { return objectiveKeys[index]; }
    uint32_t ObjectiveStatus(uint32_t index) const override { return objectiveStatus[index]; }
    int WorldCounterValue(WorldCounter counter) const override { return counters[static_cast<int>(counter)]; }
    bool SandboxMode() const override { return sandbox; }
    int CurrentScenario() const override { return scenario; }
    bool ScenarioCompleted(int s) const override { return completed[s]; }
    bool ScenarioAlwaysAvailable(int s) const override { return always[s]; }
    int GuestCapacityOfLodgings() const override { return lodgings; }
    int MeasureWidth(const obl::Char16* text) const override { return 10 * static_cast<int>(obl::text16::Length(text)); }
};

constexpr uint32_t kWidgetColor = 0x112233;

obl::Text16 Run(const char* key, const FakeEnv& env, uint32_t* color = nullptr, bool* handled = nullptr)
{
    Output out;
    out.color = 0xdeadbeef;
    const bool ok = Evaluate(key, env, kWidgetColor, out);
    if (handled) *handled = ok;
    if (color) *color = out.color;
    return ok ? obl::Text16(out.text) : obl::Text16();
}

Output RunOutput(const char* key, const FakeEnv& env)
{
    Output out;
    out.color = 0xdeadbeef;
    out.xOffset = 0x7fff;
    OBL_CHECK(Evaluate(key, env, kWidgetColor, out));
    return out;
}

} // namespace

OBL_TEST_CASE("gui_variable.player_money_formats_cash_and_colours_debt_red")
{
    FakeEnv env;
    uint32_t color = 0;
    env.cash = 25000.0f;
    OBL_CHECK(Run("PlayerMoney", env, &color) == u"25000");
    OBL_CHECK_EQ(color, kWidgetColor);
    OBL_CHECK(Run("PlayerMoney$", env, &color) == u"$25000");
    OBL_CHECK_EQ(color, kWidgetColor);

    env.cash = -1234.25f;
    OBL_CHECK(Run("PlayerMoney", env, &color) == u"-1234");
    OBL_CHECK_EQ(color, 0xff0000u);
    OBL_CHECK(Run("PlayerMoney$", env, &color) == u"$-1234");
    OBL_CHECK_EQ(color, 0xff0000u);

    env.cash = 0.0f;
    Run("PlayerMoney", env, &color);
    OBL_CHECK_EQ(color, kWidgetColor);
    env.cash = std::numeric_limits<float>::quiet_NaN();
    Run("PlayerMoney", env, &color);
    OBL_CHECK_EQ(color, 0xff0000u);
}

OBL_TEST_CASE("gui_variable.titles_use_the_game_text_as_format")
{
    FakeEnv env;
    env.cash = 1250.0f;
    env.guests = 42;
    env.day = 9;
    uint32_t color = 0;
    OBL_CHECK(Run("PlayerMoneyTitle", env, &color) == u"Cash: $1250");
    OBL_CHECK_EQ(color, kWidgetColor); // the title never turns red
    env.cash = -50.0f;
    Run("PlayerMoneyTitle", env, &color);
    OBL_CHECK_EQ(color, kWidgetColor);
    OBL_CHECK(Run("NumGuestsTitle", env) == u"Guests (42)");
    OBL_CHECK(Run("GameDayTitle", env) == u"Day 10"); // days are stored 0-based
}

OBL_TEST_CASE("gui_variable.counters_and_clock")
{
    FakeEnv env;
    env.guests = 0;
    OBL_CHECK(Run("NumGuests", env) == u"0");
    env.guests = 123456;
    OBL_CHECK(Run("NumGuests", env) == u"123456");
    env.guests = -7;
    OBL_CHECK(Run("NumGuests", env) == u"-7");
    env.guests = std::numeric_limits<int>::min();
    OBL_CHECK(Run("NumGuests", env) == u"-2147483648");

    env.day = 0;
    OBL_CHECK(Run("GameDay", env) == u"1");
    env.day = 364;
    OBL_CHECK(Run("GameDay", env) == u"365");

    env.time = 0.0f;
    OBL_CHECK(Run("Time", env) == u"0:00");
    env.time = 8.5f;
    OBL_CHECK(Run("Time", env) == u"8:30");
    env.time = 17.74f; // 44.4 minutes round down to the quarter
    OBL_CHECK(Run("Time", env) == u"17:30");
    env.time = 23.99f;
    OBL_CHECK(Run("Time", env) == u"23:45");
}

OBL_TEST_CASE("gui_variable.sound_distances_have_no_decimals")
{
    FakeEnv env;
    env.minDistance = 5.0f;
    env.maxDistance = 100.25f;
    OBL_CHECK(Run("MinDistance", env) == u"5");
    OBL_CHECK(Run("MaxDistance", env) == u"100");
}

OBL_TEST_CASE("gui_variable.unrecognized_keys_without_a_provider_draw_nothing")
{
    FakeEnv env;
    bool handled = true;
    OBL_CHECK(Run("NO_SUCH_KEY", env, nullptr, &handled).empty());
    OBL_CHECK(!handled);
    OBL_CHECK(Run("SCENARIO_OBJ", env, nullptr, &handled).empty()); // shorter than the SCENARIO_OBJ_ prefix
    OBL_CHECK(!handled);
    OBL_CHECK(Run("", env, nullptr, &handled).empty());
    OBL_CHECK(!handled);
    OBL_CHECK(Run("playermoney", env, nullptr, &handled).empty());
    OBL_CHECK(!handled);
    OBL_CHECK(Run("PlayerMoneyTitles", env, nullptr, &handled).empty());
    OBL_CHECK(!handled);
    OBL_CHECK_EQ(env.textLookups, 0);
    OBL_CHECK_EQ(env.providerAsked.size(), size_t{5});
}

OBL_TEST_CASE("gui_variable.unrecognized_keys_go_to_the_variable_provider")
{
    FakeEnv env;
    env.hasProvider = true;
    env.providerTexts["BUILDING_NAME"] = u"Beach Bar";
    env.cash = -5.0f; // a provider text never turns red
    uint32_t color = 0;
    bool handled = false;
    OBL_CHECK(Run("BUILDING_NAME", env, &color, &handled) == u"Beach Bar");
    OBL_CHECK(handled);
    OBL_CHECK_EQ(color, kWidgetColor);
    const Output out = RunOutput("BUILDING_NAME", env);
    OBL_CHECK(out.fromProvider);
    OBL_CHECK(!out.undefined);
    OBL_CHECK_EQ(out.xOffset, 0);

    OBL_CHECK(Run("GUEST_NAME", env, nullptr, &handled).empty());
    OBL_CHECK(handled);

    // The table wins over the provider: its keys never reach it.
    env.providerAsked.clear();
    Run("PlayerMoney", env);
    Run("SCENARIO_OBJ_1", env);
    OBL_CHECK(env.providerAsked.empty());
}

OBL_TEST_CASE("gui_variable.every_handled_key_is_distinct_and_evaluates")
{
    FakeEnv env;
    env.cash = 10.0f;
    env.selectedBuilding = 1; // the disco has a name text; give it appeal for both sexes so no caption stays empty
    env.buildings[1].slots = 1;
    env.buildings[1].appeal[0][0] = env.buildings[1].appeal[0][1] = 1.0f;
    env.objectiveCount = 4;
    env.scenario = 2; // the fake knows the menu texts of the third scenario only
    env.nextToday = 9.0;
    env.todayBoats = {9.0, 15.5};
    const int count = HandledKeyCount();
    OBL_CHECK(count >= 92);
    for (int i = 0; i < count; ++i) {
        bool handled = false;
        const obl::Text16 text = Run(HandledKey(i), env, nullptr, &handled);
        OBL_CHECK(handled);
        // Objective numbers past the count (and negative ones, which wrap to a huge unsigned) leave the cell empty.
        const std::string key = HandledKey(i);
        OBL_CHECK(!text.empty() || key == "SCENARIO_OBJ_4" || key == "SCENARIO_OBJ_99" || key == "SCENARIO_OBJ_-1");
        for (int j = i + 1; j < count; ++j) OBL_CHECK(std::string(HandledKey(i)) != HandledKey(j));
    }
}

OBL_TEST_CASE("gui_variable.next_ferry_tool_shows_today_s_boat_or_a_dash")
{
    FakeEnv env;
    env.nextToday = 8.5;
    OBL_CHECK(Run("NextFerryTool", env) == u"8:30");
    env.nextToday = 23.99;
    OBL_CHECK(Run("NextFerryTool", env) == u"23:45");
    for (double none : {-1.0, 0.0, std::numeric_limits<double>::quiet_NaN()}) {
        env.nextToday = none;
        OBL_CHECK(Run("NextFerryTool", env) == u"-");
    }
    OBL_CHECK_EQ(env.nextCalls.size(), 5u);
    for (int todayOnly : env.nextCalls) OBL_CHECK_EQ(todayOnly, 1);
}

OBL_TEST_CASE("gui_variable.next_ferry_falls_back_to_a_later_day")
{
    FakeEnv env;
    env.nextToday = 14.25;
    env.nextLater = 7.25;
    env.nextDay = 3;
    OBL_CHECK(Run("NextFerry", env) == u"14:15"); // a boat today wins and the later query is not made
    OBL_CHECK_EQ(env.nextCalls.size(), 1u);

    env.nextCalls.clear();
    env.nextToday = -1;
    OBL_CHECK(Run("NextFerry", env) == u"7:15 on day 4"); // the 0-based day is shown 1-based
    OBL_CHECK_EQ(env.nextCalls.size(), 2u);
    OBL_CHECK_EQ(env.nextCalls[0], 1);
    OBL_CHECK_EQ(env.nextCalls[1], 0);

    env.nextLater = -1;
    OBL_CHECK(Run("NextFerry", env).empty()); // no boat at all: an empty caption
    env.nextToday = 0.0;
    env.nextLater = 0.0;
    OBL_CHECK(Run("NextFerry", env).empty());
}

OBL_TEST_CASE("gui_variable.ferry_schedule_lists_today_s_boats")
{
    FakeEnv env;
    OBL_CHECK(Run("FerrySchedule", env).empty());
    env.todayBoats = {9.0};
    OBL_CHECK(Run("FerrySchedule", env) == u"9:00");
    env.todayBoats = {6.0, 12.5, 18.75};
    OBL_CHECK(Run("FerrySchedule", env) == u"6:00, 12:30, 18:45");
    // The list ends at the first answer of -1 or less (or NaN), whatever follows.
    env.todayBoats = {6.0, -1.0, 12.5};
    OBL_CHECK(Run("FerrySchedule", env) == u"6:00");
    env.todayBoats = {6.0, std::numeric_limits<double>::quiet_NaN(), 12.5};
    OBL_CHECK(Run("FerrySchedule", env) == u"6:00");
    // A boat at midnight is a boat (only values <= -1 end the list).
    env.todayBoats = {0.0, 1.0};
    OBL_CHECK(Run("FerrySchedule", env) == u"0:00, 1:00");
}

OBL_TEST_CASE("gui_variable.ferry_panel_captions_follow_the_wharf")
{
    FakeEnv env;
    OBL_CHECK(Run("FP_TITLE", env) == u"Bus summary");
    OBL_CHECK(Run("FP_NEXT", env) == u"Next bus");
    OBL_CHECK(Run("FP_SCHEDULE", env) == u"Bus schedule");
    OBL_CHECK(env.wharfAsked);
    env.wharf = true;
    OBL_CHECK(Run("FP_TITLE", env) == u"Ferry summary");
    OBL_CHECK(Run("FP_NEXT", env) == u"Next ferry");
    OBL_CHECK(Run("FP_SCHEDULE", env) == u"Ferry schedule");
}

OBL_TEST_CASE("gui_variable.island_captions_print_the_world_texts_as_formats")
{
    FakeEnv env;
    OBL_CHECK(Run("ISLAND_TITLE", env) == u"Sunny Cove");
    OBL_CHECK(Run("ISLAND_AREA", env) == u"Area: 40% of the bay");
    OBL_CHECK(Run("ISLAND_WEATHER", env) == u"Mild");
    OBL_CHECK(Run("ISLAND_TEMP", env) == u"25C");
    OBL_CHECK(Run("ISLAND_HAZARDS", env) == u"Sharks");
    env.islandKeys[0] = "ISL_TEMP"; // the key is whatever the world holds
    OBL_CHECK(Run("ISLAND_TITLE", env) == u"25C");
}

OBL_TEST_CASE("gui_variable.script_and_message_texts_are_printed_verbatim")
{
    FakeEnv env;
    OBL_CHECK(Run("ScriptText", env) == u"Welcome 100% %d"); // "%s": nothing in the text is interpreted
    OBL_CHECK(Run("MessageText", env) == u"Message text");
    env.messageTextKey = "SCRIPT_KEY";
    OBL_CHECK(Run("MessageText", env) == u"Welcome 100% %d");
}

OBL_TEST_CASE("gui_variable.main_version_formats_the_game_version")
{
    FakeEnv env;
    OBL_CHECK(Run("MAIN_VERSION", env) == u"Version 1.1");
    env.version = 2.0;
    OBL_CHECK(Run("MAIN_VERSION", env) == u"Version 2.0");
}

OBL_TEST_CASE("gui_variable.object_browser_captions")
{
    FakeEnv env;
    OBL_CHECK(Run("OB_FILTER_HEADING", env) == u"Happiness");
    env.filterHeading = nullptr; // no heading for the active filter: an empty caption, and no text lookup
    env.textLookups = 0;
    OBL_CHECK(Run("OB_FILTER_HEADING", env).empty());
    OBL_CHECK_EQ(env.textLookups, 0);
    env.browserCount = 17;
    OBL_CHECK(Run("OB_GUEST_NUM", env) == u"17 guests");
    OBL_CHECK(Run("OB_BUILDING_NUM", env) == u"17 buildings");
    env.browserCount = -3;
    OBL_CHECK(Run("OB_GUEST_NUM", env) == u"-3 guests");
}

OBL_TEST_CASE("gui_variable.ferry_schedule_stops_at_the_buffer_capacity")
{
    FakeEnv env;
    env.todayBoats.assign(600, 10.0);
    const obl::Text16 text = Run("FerrySchedule", env);
    OBL_CHECK_EQ(text.size(), static_cast<size_t>(kTextCapacity - 1));
    OBL_CHECK(text.substr(0, 11) == u"10:00, 10:0");
}

OBL_TEST_CASE("gui_variable.power_is_truncated_like_truncation")
{
    FakeEnv env;
    env.powerOutput = 12.9f;
    env.powerDemand = -3.7f;
    env.powerUse = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(Run("WORLD_POWER_OUTPUT", env) == u"12");
    OBL_CHECK(Run("WORLD_POWER_DEMAND", env) == u"-3");
    OBL_CHECK(Run("WORLD_POWER_USE", env) == u"0"); // the float-to-int conversion answers the low 32 bits of "integer indefinite" for NaN
    env.powerUse = 3e9f;
    OBL_CHECK(Run("WORLD_POWER_USE", env) == u"-1294967296"); // beyond int it wraps (low 32 bits of the 64-bit value)
    env.powerUse = -3e9f;
    OBL_CHECK(Run("WORLD_POWER_USE", env) == u"1294967296");
    env.powerUse = 1e20f; // beyond 64 bits too
    OBL_CHECK(Run("WORLD_POWER_USE", env) == u"0");
}

OBL_TEST_CASE("gui_variable.weather_channels_come_from_the_packed_colour")
{
    FakeEnv env;
    env.weatherColor = 0x7f123456; // the top byte is not a channel
    OBL_CHECK(Run("WEATHER_RED", env) == u"18");
    OBL_CHECK(Run("WEATHER_GREEN", env) == u"52");
    OBL_CHECK(Run("WEATHER_BLUE", env) == u"86");
    env.weatherColor = 0xff00ff;
    OBL_CHECK(Run("WEATHER_GREEN", env) == u"0");
    OBL_CHECK(Run("WEATHER_RED", env) == u"255");
}

OBL_TEST_CASE("gui_variable.object_counters")
{
    FakeEnv env;
    env.litter = 12;
    env.classCounts[2] = 31;
    for (int objectClass = 7; objectClass <= 12; ++objectClass) env.classCounts[objectClass] = objectClass; // 7+8+9+10+11+12
    env.classCounts[6] = 1000; // service staff is not counted
    env.classCounts[13] = 1000;
    env.sceneryPlain = 77;
    env.guests = 9;
    env.zeroGender = 4;
    env.nonZeroGender = 5;
    OBL_CHECK(Run("LITTER_COUNT", env) == u"12 litter items");
    OBL_CHECK(Run("LITTER_COUNT_NOFORMAT", env) == u"12");
    OBL_CHECK(Run("BUILDING_COUNT", env) == u"31");
    OBL_CHECK(Run("STAFF_COUNT", env) == u"57");
    OBL_CHECK(Run("SCENERY_COUNT", env) == u"77 scenery objects");
    OBL_CHECK(Run("GUEST_BREAKDOWN", env) == u"9 guests: 4 women, 5 men");
}

OBL_TEST_CASE("gui_variable.guest_flow_texts")
{
    FakeEnv env;
    const auto set = [&](GuestFlowField field, int first, int second) {
        env.flows[static_cast<int>(field)][0] = first;
        env.flows[static_cast<int>(field)][1] = second;
    };
    set(GuestFlowField::TotalArrive, 10, 4);
    set(GuestFlowField::TodayArrive, 3, 2);
    set(GuestFlowField::TotalDepart, 200, 100);
    set(GuestFlowField::TodayDepart, 0, 0);
    set(GuestFlowField::TotalUnhappyDepart, 2, 1);
    set(GuestFlowField::TodayUnhappyDepart, 5, 5);
    OBL_CHECK(Run("ARRIVALS_TOTAL", env) == u"14 (10/4)");
    OBL_CHECK(Run("ARRIVALS_TODAY", env) == u"5 (3/2)");
    OBL_CHECK(Run("DEPART_TOTAL", env) == u"300 (200/100)");
    OBL_CHECK(Run("DEPART_TODAY", env) == u"0 (0/0)");
    OBL_CHECK(Run("UN_DEPART_TOTAL", env) == u"3 (2/1) 1%");
    OBL_CHECK(Run("UN_DEPART_TODAY", env) == u"10 (5/5) 0%");
    set(GuestFlowField::TotalDepart, 7, 3);
    set(GuestFlowField::TotalUnhappyDepart, 5, 0);
    OBL_CHECK(Run("UN_DEPART_TOTAL", env) == u"5 (5/0) 50%");
    set(GuestFlowField::TotalDepart, 3, 0);
    set(GuestFlowField::TotalUnhappyDepart, 1, 0);
    OBL_CHECK(Run("UN_DEPART_TOTAL", env) == u"1 (1/0) 33%");
}

OBL_TEST_CASE("gui_variable.world_money_keys_colour_only_income_like_values")
{
    FakeEnv env;
    uint32_t color = 0;
    const auto at = [&](MoneyField field) -> float& { return env.money[static_cast<int>(field)]; };
    at(MoneyField::Income) = 1500.25f;
    OBL_CHECK(Run("WORLD_INCOME_$", env, &color) == u"$1500");
    OBL_CHECK_EQ(color, kWidgetColor);
    at(MoneyField::Income) = -20.25f;
    OBL_CHECK(Run("WORLD_INCOME_$", env, &color) == u"$-20");
    OBL_CHECK_EQ(color, 0xff0000u);

    // Costs are stored as positive amounts and never shown red, even when negative.
    at(MoneyField::Costs) = -20.25f;
    OBL_CHECK(Run("WORLD_COST_$", env, &color) == u"$-20");
    OBL_CHECK_EQ(color, kWidgetColor);
    at(MoneyField::ConstructCost) = 800.0f;
    at(MoneyField::SceneryCost) = 60.0f;
    at(MoneyField::WagesCost) = 120.0f;
    at(MoneyField::StockCost) = 33.0f;
    at(MoneyField::AverageCost) = -7.0f;
    OBL_CHECK(Run("WORLD_COST_CON_$", env) == u"$800");
    OBL_CHECK(Run("WORLD_COST_SCEN_$", env) == u"$60");
    OBL_CHECK(Run("WORLD_COST_WAGES_$", env) == u"$120");
    OBL_CHECK(Run("WORLD_COST_STOCK_$", env) == u"$33");
    Run("WORLD_COST_AVG_$", env, &color);
    OBL_CHECK_EQ(color, kWidgetColor);

    at(MoneyField::DemolishIncome) = -3.0f;
    at(MoneyField::FineIncome) = -4.0f;
    Run("WORLD_INCOME_DEMOLISH_$", env, &color);
    OBL_CHECK_EQ(color, kWidgetColor);
    Run("WORLD_INCOME_FINE_$", env, &color);
    OBL_CHECK_EQ(color, kWidgetColor);

    const char* const colored[] = {
            "WORLD_INCOME_ACC_$", "WORLD_INCOME_ATTRACT_$", "WORLD_INCOME_RETAIL_$", "WORLD_PROFIT_AVG_$", "WORLD_INCOME_AVG_$"};
    const MoneyField fields[] = {MoneyField::AccomIncome, MoneyField::AttractIncome, MoneyField::RetailIncome, MoneyField::AverageProfit,
            MoneyField::AverageIncome};
    for (int i = 0; i < 5; ++i) {
        at(fields[i]) = 12.0f;
        Run(colored[i], env, &color);
        OBL_CHECK_EQ(color, kWidgetColor);
        at(fields[i]) = -12.0f;
        OBL_CHECK(Run(colored[i], env, &color) == u"$-12");
        OBL_CHECK_EQ(color, 0xff0000u);
    }
}

OBL_TEST_CASE("gui_variable.profit_is_income_minus_costs")
{
    FakeEnv env;
    uint32_t color = 0;
    env.money[static_cast<int>(MoneyField::Income)] = 100.25f;
    env.money[static_cast<int>(MoneyField::Costs)] = 250.5f;
    OBL_CHECK(Run("WORLD_PROFIT_$", env, &color) == u"$-150");
    OBL_CHECK_EQ(color, 0xff0000u);
    env.money[static_cast<int>(MoneyField::Costs)] = 100.25f;
    OBL_CHECK(Run("WORLD_PROFIT_$", env, &color) == u"$0");
    OBL_CHECK_EQ(color, kWidgetColor);
    env.money[static_cast<int>(MoneyField::Costs)] = 0.0f;
    OBL_CHECK(Run("WORLD_PROFIT_$", env, &color) == u"$100");
    OBL_CHECK_EQ(color, kWidgetColor);
}

OBL_TEST_CASE("gui_variable.build_panel_texts_come_from_the_selected_building")
{
    FakeEnv env;
    uint32_t color = 0;
    env.selectedBuilding = 1;
    OBL_CHECK(Run("BUILD_BUILDING_DESC", env, &color) == u"A disco");
    OBL_CHECK_EQ(color, kWidgetColor);
    OBL_CHECK(Run("BUILD_NAME_TEXT", env, &color) == u"Disco Inferno");
    OBL_CHECK_EQ(color, kWidgetColor);
    env.selectedBuilding = 0;
    OBL_CHECK(Run("BUILD_BUILDING_DESC", env) == u"A bar");
    OBL_CHECK(Run("BUILD_NAME_TEXT", env) == u""); // no BUILDING_NAME_BAR text: the text lookup routine's empty string
}

OBL_TEST_CASE("gui_variable.build_cost_turns_red_when_cash_falls_short")
{
    FakeEnv env;
    uint32_t color = 0;
    env.selectedBuilding = 1;
    env.cash = 9000.0f;
    OBL_CHECK(Run("BUILD_COST_AMOUNT", env, &color) == u"9000");
    OBL_CHECK_EQ(color, kWidgetColor); // exactly the price still buys it
    env.cash = 8999.5f;
    Run("BUILD_COST_AMOUNT", env, &color);
    OBL_CHECK_EQ(color, 0xff0000u);
    env.cash = std::numeric_limits<float>::quiet_NaN();
    Run("BUILD_COST_AMOUNT", env, &color);
    OBL_CHECK_EQ(color, 0xff0000u);

    env.selectedScenery = 1;
    env.cash = 250.0f;
    OBL_CHECK(Run("SP_SCENERY_COST", env, &color) == u"250");
    OBL_CHECK_EQ(color, kWidgetColor);
    env.cash = 249.0f;
    Run("SP_SCENERY_COST", env, &color);
    OBL_CHECK_EQ(color, 0xff0000u);
}

OBL_TEST_CASE("gui_variable.build_power_is_the_absolute_value")
{
    FakeEnv env;
    env.selectedBuilding = 1;
    OBL_CHECK(Run("BUILD_POWER_AMOUNT", env) == u"35"); // a generator: negative in the data
    env.selectedBuilding = 0;
    OBL_CHECK(Run("BUILD_POWER_AMOUNT", env) == u"20");
    env.buildings[0].power = std::numeric_limits<int>::min();
    OBL_CHECK(Run("BUILD_POWER_AMOUNT", env) == u"-2147483648");
}

OBL_TEST_CASE("gui_variable.build_tree_shows_the_preceding_building_and_marks_it_missing")
{
    FakeEnv env;
    uint32_t color = 0;
    env.selectedBuilding = 1;
    OBL_CHECK(Run("BUILD_BUILDING_TREE", env, &color) == u"Beach Bar");
    OBL_CHECK_EQ(color, kWidgetColor);
    OBL_CHECK(env.lastIdleKey == "BAR");

    env.idleBuilding = false; // none built, or only ones still under construction or demolition
    OBL_CHECK(Run("BUILD_BUILDING_TREE", env, &color) == u"Beach Bar");
    OBL_CHECK_EQ(color, 0xff0000u);

    env.idleBuilding = true;
    env.buildings[1].allowed = false;
    Run("BUILD_BUILDING_TREE", env, &color);
    OBL_CHECK_EQ(color, 0xff0000u);
}

OBL_TEST_CASE("gui_variable.build_hours_pick_the_text_of_the_opening_hours_set")
{
    FakeEnv env;
    const struct {
        const char* hours;
        const obl::Char16* text;
    } cases[] = {{"ALLHOURS", u"All day"}, {"MORNING", u"Mornings"}, {"DAY", u"Daytime"}, {"EVENING", u"Evenings"}, {"NIGHT", u"Nights"}};
    for (const auto& entry : cases) {
        env.buildings[0].hours = entry.hours;
        bool handled = false;
        OBL_CHECK(Run("BUILD_BUILDING_HOURS", env, nullptr, &handled) == entry.text);
        OBL_CHECK(handled);
    }
    env.buildings[0].hours = "LUNCH";
    const Output out = RunOutput("BUILD_BUILDING_HOURS", env);
    OBL_CHECK(out.undefined);
    OBL_CHECK(out.text[0] == u'\0');
    OBL_CHECK(env.providerAsked.empty());
}

OBL_TEST_CASE("gui_variable.scenery_description_key_is_upper_cased")
{
    FakeEnv env;
    env.selectedScenery = 0;
    OBL_CHECK(Run("SP_SCENERY_DESC", env) == u"A bin");
    env.selectedScenery = 1;
    OBL_CHECK(Run("SP_SCENERY_DESC", env) == u"A palm tree");
}

OBL_TEST_CASE("gui_variable.buildmenu_appeal_caption_needs_a_positive_mean")
{
    FakeEnv env;
    env.selectedBuilding = 0;
    OBL_CHECK(Run("BUILDMENU_MALE", env) == u"Appeals");
    OBL_CHECK(Run("BUILDMENU_FEMALE", env) == u""); // 0 is not positive

    env.buildings[0].slots = 3;
    env.buildings[0].appeal[0][1] = 2.0f;
    env.buildings[0].appeal[1][1] = -3.0f;
    env.buildings[0].appeal[2][1] = 0.5f;
    OBL_CHECK(Run("BUILDMENU_FEMALE", env) == u""); // (2 - 3 + 0.5) / 3 < 0
    env.buildings[0].appeal[1][1] = -1.0f;
    OBL_CHECK(Run("BUILDMENU_FEMALE", env) == u"Appeals");

    // Without commodity slots the mean is -1 for both sexes.
    env.selectedBuilding = 1;
    OBL_CHECK(Run("BUILDMENU_MALE", env) == u"");
    OBL_CHECK(Run("BUILDMENU_FEMALE", env) == u"");
}

OBL_TEST_CASE("gui_variable.scenario_captions_leave_room_for_their_heading")
{
    FakeEnv env;
    Output out = RunOutput("SCENARIO_DESCRIPTION", env);
    OBL_CHECK(obl::Text16(out.text) == u"Build a resort");
    OBL_CHECK_EQ(out.xOffset, 40); // "Goal" measured by the fake at 10 px a character
    OBL_CHECK_EQ(out.color, kWidgetColor);

    out = RunOutput("SCENARIO_OBJECTIVES", env);
    OBL_CHECK(obl::Text16(out.text) == u"Make money");
    OBL_CHECK_EQ(out.xOffset, 50);

    // Keys that carry no heading keep the whole rectangle.
    OBL_CHECK_EQ(RunOutput("PlayerMoney", env).xOffset, 0);
    OBL_CHECK_EQ(RunOutput("OBJECTIVES_DONE", env).xOffset, 0);
}

OBL_TEST_CASE("gui_variable.objective_lines_are_indexed_by_the_number_in_the_key")
{
    FakeEnv env;
    env.objectiveCount = 3;
    Output out = RunOutput("SCENARIO_OBJ_0", env);
    OBL_CHECK(obl::Text16(out.text) == u"Objective A");
    OBL_CHECK_EQ(out.xOffset, 80 + 0x23); // "Progress" heading plus the tick-box column
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_2", env).text) == u"Objective C");
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_+1", env).text) == u"Objective B"); // atoi takes a sign
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_ 1", env).text) == u"Objective B"); // and leading white space
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_1x", env).text) == u"Objective B"); // and stops at the first non-digit
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_", env).text) == u"Objective A"); // no digits is 0

    // Past the count or negative (a huge unsigned): an empty cell with no heading offset.
    for (const char* key : {"SCENARIO_OBJ_3", "SCENARIO_OBJ_99", "SCENARIO_OBJ_-1"}) {
        const Output past = RunOutput(key, env);
        OBL_CHECK(obl::Text16(past.text).empty());
        OBL_CHECK_EQ(past.xOffset, 0);
    }
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_4294967296", env).text) == u"Objective A"); // atoi wraps modulo 2^32 ...
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_4294967297", env).text) == u"Objective B"); // ... instead of saturating
    env.objectiveCount = 0;
    OBL_CHECK(obl::Text16(RunOutput("SCENARIO_OBJ_0", env).text).empty());
}

OBL_TEST_CASE("gui_variable.objectives_done_counts_flags_equal_to_one_within_the_count")
{
    FakeEnv env;
    env.objectiveStatus[0] = 1;
    env.objectiveStatus[1] = 2; // anything but 1 is not done
    env.objectiveStatus[2] = 1;
    env.objectiveStatus[3] = 1;
    env.objectiveCount = 3;
    OBL_CHECK(Run("OBJECTIVES_DONE", env) == u"2");
    env.objectiveCount = 4;
    OBL_CHECK(Run("OBJECTIVES_DONE", env) == u"3");
    env.objectiveCount = 0;
    OBL_CHECK(Run("OBJECTIVES_DONE", env) == u"0");
}

OBL_TEST_CASE("gui_variable.scenario_title_depends_on_the_sandbox_flag")
{
    FakeEnv env;
    env.scenario = 2; // 0-based, the text keys count from 1
    OBL_CHECK(Run("SCENARIO_TITLE", env) == u"Beach Basics");
    env.sandbox = true;
    OBL_CHECK(Run("SCENARIO_TITLE", env) == u"Sandbox three");
}

OBL_TEST_CASE("gui_variable.menu_scenario_texts_follow_the_picked_scenario")
{
    FakeEnv env;
    env.scenario = 2;
    OBL_CHECK(Run("SS_CUR_SCENARIO_TITLE", env) == u"Scenario three");
    OBL_CHECK(Run("SS_CUR_SCENARIO_DESC", env) == u"About scenario three");
    env.sandbox = true;
    OBL_CHECK(Run("SS_CUR_SCENARIO_TITLE", env) == u"Sandbox three");
    OBL_CHECK(Run("SS_CUR_SCENARIO_DESC", env) == u"About sandbox three");
    OBL_CHECK(Run("SS_CUR_SCENARIO_STATUS", env).empty()); // a sandbox has no completion state
}

OBL_TEST_CASE("gui_variable.menu_scenario_status_prefers_completed_over_always_open")
{
    FakeEnv env;
    env.scenario = 4;
    OBL_CHECK(Run("SS_CUR_SCENARIO_STATUS", env) == u"Not completed");
    env.always[4] = true;
    OBL_CHECK(Run("SS_CUR_SCENARIO_STATUS", env) == u"Always open");
    env.completed[4] = true;
    OBL_CHECK(Run("SS_CUR_SCENARIO_STATUS", env) == u"Complete");
    env.completed[4] = false;
    env.always[3] = true; // another scenario does not count
    env.always[4] = false;
    OBL_CHECK(Run("SS_CUR_SCENARIO_STATUS", env) == u"Not completed");
}

OBL_TEST_CASE("gui_variable.scenario_counters_read_their_world_fields")
{
    FakeEnv env;
    const struct {
        const char* key;
        WorldCounter counter;
    } cases[] = {{"BUILDING_CONSTRUCTED", WorldCounter::BuildingsConstructed},
            {"BUILDING_DIFF_CONSTRUCTED", WorldCounter::DiffBuildingsConstructed}, {"BAR_COUNT", WorldCounter::BarsConstructed},
            {"GUEST_PARTYCOUNT", WorldCounter::PartyCount}, {"SHAG_COUNT", WorldCounter::ShagCount},
            {"PISSED_COUNT", WorldCounter::PissedCount}, {"GUESTS_DROWNED", WorldCounter::GuestsDrowned},
            {"GUESTS_EATEN", WorldCounter::GuestsEaten}};
    int value = 11;
    for (const auto& entry : cases) {
        env.counters[static_cast<int>(entry.counter)] = value;
        OBL_CHECK(Run(entry.key, env) == obl::text16::Decimal(value));
        value += 7;
    }
    env.counters[static_cast<int>(WorldCounter::GuestsDrowned)] = -3;
    OBL_CHECK(Run("GUESTS_DROWNED", env) == u"-3");

    env.classCounts[8] = 5; // mechanics are object class 8
    OBL_CHECK(Run("MECHANIC_COUNT", env) == u"5");
    env.lodgings = 120;
    OBL_CHECK(Run("NUM_APARTMENTS", env) == u"120");
}

OBL_TEST_CASE("gui_variable.unhappy_leavers_add_both_kinds_of_guest")
{
    FakeEnv env;
    env.flows[static_cast<int>(GuestFlowField::TotalUnhappyDepart)][0] = 4;
    env.flows[static_cast<int>(GuestFlowField::TotalUnhappyDepart)][1] = 9;
    env.flows[static_cast<int>(GuestFlowField::TodayUnhappyDepart)][0] = 100; // only the running total counts
    OBL_CHECK(Run("UNHAPPY_LEAVERS", env) == u"13");
    env.flows[static_cast<int>(GuestFlowField::TotalUnhappyDepart)][0] = 0x7fffffff;
    OBL_CHECK(Run("UNHAPPY_LEAVERS", env) == u"-2147483640");
}

OBL_TEST_CASE("gui_variable.build_tree_progress_is_started_minus_left_to_build")
{
    FakeEnv env;
    env.counters[static_cast<int>(WorldCounter::UnbuiltAfterStartup)] = 12;
    env.counters[static_cast<int>(WorldCounter::BuildingsLeftToBuild)] = 5;
    OBL_CHECK(Run("BUILDING_TREE_COMPELETED", env) == u"7 of 12 built");
    env.counters[static_cast<int>(WorldCounter::BuildingsLeftToBuild)] = 20;
    OBL_CHECK(Run("BUILDING_TREE_COMPELETED", env) == u"-8 of 12 built");

    env.counters[static_cast<int>(WorldCounter::DiffBuildingsConstructed)] = 4;
    OBL_CHECK(Run("PM_5_UNIQUE", env) == u"4 unique");
}

OBL_TEST_CASE("gui_variable.fine_income_is_truncated_like_truncation")
{
    FakeEnv env;
    uint32_t color = 0;
    env.money[static_cast<int>(MoneyField::TotalFines)] = 1234.9f;
    OBL_CHECK(Run("FINE_INCOME_$", env, &color) == u"$1234");
    env.money[static_cast<int>(MoneyField::TotalFines)] = -5.5f;
    OBL_CHECK(Run("FINE_INCOME_$", env, &color) == u"$-5");
    OBL_CHECK_EQ(color, kWidgetColor); // the fines never turn red
    env.money[static_cast<int>(MoneyField::TotalFines)] = 3e9f;
    OBL_CHECK(Run("FINE_INCOME_$", env) == u"$-1294967296"); // the low 32 bits, as in the power texts
    env.money[static_cast<int>(MoneyField::TotalFines)] = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(Run("FINE_INCOME_$", env) == u"$0");
}

OBL_TEST_CASE("gui_variable.staff_amounts_count_hired_staff_against_their_homes")
{
    FakeEnv env;
    env.staffCategory = 6; // everyone
    env.staffAll = 23;
    OBL_CHECK(Run("STAFF_AMOUNTS", env) == u"23 staff");

    // Category -> object class, home building and texts.
    struct Case {
        int category, objectClass;
        const char* home;
        const obl::Char16* one;
        const obl::Char16* many;
    };
    const Case cases[] = {{0, 0xb, "CLEANERSHACK", u"cleaner", u"cleaners"}, {1, 0xa, "BUILDERYARD", u"builder", u"builders"},
            {2, 8, "HANDYMAN", u"mechanic", u"mechanics"}, {3, 9, "REP", u"rep", u"reps"}, {4, 7, "BAYWATCH", u"lifeguard", u"lifeguards"},
            {5, 0xc, "SECURITY_POST", u"guard", u"guards"}};
    env.maxStaffPerHome = 4;
    for (const Case& c : cases) {
        env = FakeEnv();
        env.maxStaffPerHome = 4;
        env.staffCategory = c.category;
        env.idleHomes[c.home] = 3;
        env.payroll[c.objectClass] = 1; // one worker: the singular title
        OBL_CHECK(Run("STAFF_AMOUNTS", env) == obl::Text16(u"1 ") + c.one + u" (max 12)");
        OBL_CHECK_EQ(env.maxStaffKey, std::string(c.home));
        env.payroll[c.objectClass] = 5;
        OBL_CHECK(Run("STAFF_AMOUNTS", env) == obl::Text16(u"5 ") + c.many + u" (max 12)");
        env.payroll[c.objectClass] = 0; // nobody: the plural too
        OBL_CHECK(Run("STAFF_AMOUNTS", env) == obl::Text16(u"0 ") + c.many + u" (max 12)");
    }

    // The limit is the per-home staff times the finished homes, wrapping in 32 bits.
    env = FakeEnv();
    env.staffCategory = 2;
    env.payroll[8] = 2;
    OBL_CHECK(Run("STAFF_AMOUNTS", env) == u"2 mechanics (max 0)"); // no homes
    env.idleHomes["HANDYMAN"] = 2;
    env.maxStaffPerHome = 0x40000001;
    OBL_CHECK(Run("STAFF_AMOUNTS", env) == u"2 mechanics (max -2147483646)");
}

OBL_TEST_CASE("gui_variable.staff_amounts_outside_the_seven_categories_are_undefined")
{
    FakeEnv env;
    for (int category : {7, 100, -1}) {
        env.staffCategory = category;
        const Output out = RunOutput("STAFF_AMOUNTS", env);
        OBL_CHECK(out.undefined);
        OBL_CHECK(out.text[0] == u'\0');
    }
}
