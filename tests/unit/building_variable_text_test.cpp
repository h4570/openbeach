#include "core/text16.h"
#include "game/world/world_state.h"
#include "unit_test.h"
#include "game/objects/building_variable_text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>

using namespace obl;

namespace {
struct Env {
    std::string lookedUp, hours = "ALLHOURS";
    Text16 answer;
    const Char16* Lookup(const char* key)
    {
        lookedUp = key;
        answer.assign(lookedUp.begin(), lookedUp.end());
        return answer.c_str();
    }
    const char* HoursName(int) { return hours.c_str(); }
    void PrintInt(Char16* out, const Char16* fmt, int value) { obl::text16::Format(out, 64, fmt, value); }
    void PrintReal(Char16* out, const Char16* fmt, double value) { obl::text16::Format(out, 64, fmt, value); }
    void PrintMoney(Char16* out, const Char16* fmt, int dollars, int cents) { obl::text16::Format(out, 64, fmt, dollars, cents); }
    void PrintInt3(Char16* out, const Char16*, int a, int b, int c) { obl::text16::Format(out, 64, u"%d:%d:%d", a, b, c); }
    int WholeHour(float time) { return static_cast<int>(time); }
    int QuarterMinutes(float time) { return static_cast<int>((time - std::floor(time)) * 4.0f) * 15; }
    int repairStatus = 0, cleaningStatus = 0, staff = 1;
    int RepairJobStatus(int) { return repairStatus; }
    int CleaningJobStatus(int) { return cleaningStatus; }
    int CountByClass(int) { return staff; }
    void PrintString(Char16* out, const Char16*, const Char16* text) { obl::text16::Format(out, 64, u"[%ls]", text); }
    std::string displayNames[3] = {"Food", "Beer", "Drink"};
    const char* CommodityDisplayName(int id) { return displayNames[id].c_str(); }
    const char* CommodityName(int id) { return id == 0 ? "FOOD" : "DRINK"; }
    tables::BeerDef beer = {};
    const tables::BeerDef* Beer(int) { return &beer; }
    Text16 guestName = u"Ann";
    const Char16* ObjectName(int32_t) { return guestName.c_str(); }
    int hour = 12;
    int Hour() { return hour; }
    float opening = 8.0f, closing = 20.0f;
    float OpeningStart(int) { return opening; }
    float OpeningEnd(int) { return closing; }
    world_data::World world = {};
    const world_data::World& GameWorld() { return world; }
    float build = 0.0f, demolish = 0.0f;
    float BuildProgress(int) { return build; }
    float DemolishProgress(int) { return demolish; }
    bool areaOpen[2] = {true, true};
    int areaCalls = 0;
    bool AreaOpen(int kind, int)
    {
        ++areaCalls;
        return areaOpen[kind];
    }
    std::string fatalKey;
    void Fatal(const char* key) { fatalKey = key; }
};
struct Fixture {
    std::unique_ptr<object::Building> building = std::make_unique<object::Building>();
    std::unique_ptr<tables::BuildingDef> def = std::make_unique<tables::BuildingDef>();
    Env env;
    Char16 out[512] = {};
    Fixture() { building->m_definition = def.get(); }
    void Text(const char* key)
    {
        std::fill_n(out, 512, Char16{0x7777});
        building_text::GetVariableText(*building, key, out, env);
    }
    Text16 Money(float value)
    {
        building->m_incomeToday = value;
        Text("BUILDING_INCOMETODAY$");
        return out;
    }
};
} // namespace

OBL_TEST_CASE("building_text.name_and_open")
{
    Fixture f;
    f.def->m_key = "BAR";
    f.Text("BUILDING_NAME");
    OBL_CHECK_EQ(f.env.lookedUp, "BUILDING_NAME_BAR");
    f.building->m_OpenStatus = 1;
    f.Text("BUILDING_OPEN");
    OBL_CHECK_EQ(f.env.lookedUp, "CLOSED");
    f.building->m_OpenStatus = 2;
    f.Text("BUILDING_OPEN");
    OBL_CHECK(f.out[0] == Char16{0x7777});
}

OBL_TEST_CASE("building_text.opening_hours_bands")
{
    Fixture f;
    const char* const kNames[][2] = {{"ALLHOURS", "BI_HOURS_ALLDAY"}, {"MORNING", "BI_HOURS_MORNING"}, {"DAY", "BI_HOURS_DAY"},
            {"EVENING", "BI_HOURS_EVENING"}, {"NIGHT", "BI_HOURS_NIGHT"}};
    for (const auto& pair : kNames) {
        f.env.hours = pair[0];
        f.Text("BUILDING_OPENHOURS");
        OBL_CHECK_EQ(f.env.lookedUp, pair[1]);
    }
    f.env.hours = "SIESTA";
    f.env.lookedUp.clear();
    f.Text("BUILDING_OPENHOURS");
    OBL_CHECK(Text16(f.out) == u"HOURS KEYWORD NOT RECOGNISED");
    OBL_CHECK(f.env.lookedUp.empty());
}

OBL_TEST_CASE("building_text.guests_and_satisfaction")
{
    Fixture f;
    f.def->m_GuestCapacity = 24;
    f.building->m_NumGuests = -3;
    f.building->m_SatisfactionPercentage = 87.5f;
    f.Text("BUILDING_MAXGUESTS");
    OBL_CHECK(Text16(f.out) == u"24");
    f.Text("BUILDING_CURRENTGUESTS");
    OBL_CHECK(Text16(f.out) == u"-3");
    f.Text("BUILDING_SATISFACTION");
    OBL_CHECK(Text16(f.out) == u"87.50");
}

OBL_TEST_CASE("building_text.money_rounding_signs_and_edges")
{
    Fixture f;
    OBL_CHECK(f.Money(12.34f) == u"$12.34");
    OBL_CHECK(f.Money(0.0f) == u"$0.00");
    OBL_CHECK(f.Money(-12.34f) == u"-$12.34");
    OBL_CHECK(f.Money(-0.5f) == u"-$0.50");
    OBL_CHECK(f.Money(1.0f) == u"$1.00");
    // The truncated text never keeps more than two decimals after the last point.
    OBL_CHECK(f.Money(0.999f).size() <= 6);
    // NaN counts as negative; its cents are the "integer indefinite" (INT_MIN), left as such by abs.
    OBL_CHECK(f.Money(std::numeric_limits<float>::quiet_NaN())[0] == u'-');
    OBL_CHECK_EQ(building_text::RoundToInt(2.5f), 2);
    OBL_CHECK_EQ(building_text::RoundToInt(3.5f), 4);
    OBL_CHECK_EQ(building_text::RoundToInt(std::numeric_limits<float>::infinity()), std::numeric_limits<int>::min());
}

OBL_TEST_CASE("building_text.power_and_title")
{
    Fixture f;
    f.building->m_PowerConsumption = -7;
    f.Text("BUILDING_POWER");
    OBL_CHECK(Text16(f.out) == u"7");
    f.building->m_OpenStatus = 1;
    f.Text("BUILDING_POWER");
    OBL_CHECK(Text16(f.out) == u"0");
    f.building->m_PowerConsumption = 12;
    f.Text("BUILDING_POWER");
    OBL_CHECK(Text16(f.out) == u"12");
    f.building->m_PowerConsumption = -1;
    f.Text("BUILDING_POWER_TITLE");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_POWER_OUTPUT");
    f.building->m_PowerConsumption = 0;
    f.Text("BUILDING_POWER_TITLE");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_POWER");
}

OBL_TEST_CASE("building_text.output_power_scales_with_wear")
{
    Fixture f;
    f.def->m_Power = -100;
    const Char16* const kExpected[] = {u"100", u"75", u"50", u"25", u"100"};
    for (uint32_t state = 0; state < 5; ++state) {
        f.building->m_RepairState = state;
        f.building->m_OpenStatus = 0;
        f.Text("BUILDING_OUTPUT_POWER");
        OBL_CHECK(Text16(f.out) == kExpected[state]);
    }
    f.building->m_OpenStatus = 1;
    f.Text("BUILDING_OUTPUT_POWER");
    OBL_CHECK(Text16(f.out) == u"0");
    f.def->m_Power = 40;
    f.Text("BUILDING_OUTPUT_POWER");
    OBL_CHECK(Text16(f.out) == u"40");
}

OBL_TEST_CASE("building_text.age_and_event_times")
{
    Fixture f;
    f.building->m_BuildingAge = 0;
    f.Text("BUILDING_AGE");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_BUILD_AGE_NEW");
    f.building->m_BuildingAge = 1;
    f.Text("BUILDING_AGE");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_BUILD_AGE_1");
    f.building->m_BuildingAge = 5;
    f.Text("BUILDING_AGE");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_BUILD_AGE_N");
    f.building->m_lastFaultDayTime = -1.0f;
    f.Text("BUILDING_LAST_FAULT");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_LAST_FAULT_NONE");
    f.building->m_lastFaultDayTime = std::numeric_limits<float>::quiet_NaN();
    f.Text("BUILDING_LAST_FAULT");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_LAST_FAULT_NONE");
    f.building->m_lastFaultDayTime = 13.5f;
    f.building->m_LastFaultDay = 3;
    f.Text("BUILDING_LAST_FAULT");
    OBL_CHECK(Text16(f.out) == u"13:30:4");
    f.building->m_lastRepairDayTime = 0.0f;
    f.building->m_LastRepairDay = 0;
    f.Text("BUILDING_LASTINSPECT");
    OBL_CHECK(Text16(f.out) == u"0:0:1");
    f.building->m_lastRepairDayTime = -0.5f;
    f.Text("BUILDING_LASTINSPECT");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_LAST_INSPECT_NONE");
}

OBL_TEST_CASE("building_text.repair_and_cleaning_status")
{
    Fixture f;
    f.building->m_RepairState = 2;
    f.Text("BUILDING_REPAIRSTATE");
    OBL_CHECK_EQ(f.env.lookedUp, "DANGEROUS");
    f.building->m_RepairState = 4;
    f.Text("BUILDING_REPAIRSTATE");
    OBL_CHECK(f.out[0] == Char16{0x7777});
    const char* const kIdle[] = {"BI_REPAIR_NO_MECHANICS", "BI_REPAIR_CANT_REACH", "BI_REPAIR_NOT_NEEDED", "BI_REPAIR_REQUIRED",
            "BI_INSPECT_REQUIRED", "BI_REPAIR_MECHANIC_COMING", "BI_REPAIR_REPAIRING"};
    for (int status = 0; status < 7; ++status) {
        f.env.repairStatus = status;
        f.Text("BUILDING_REPAIRSTATUS");
        OBL_CHECK_EQ(f.env.lookedUp, kIdle[status]);
    }
    f.building->m_urgentRepairCalled = 1;
    f.env.repairStatus = 2;
    f.Text("BUILDING_REPAIRSTATUS");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_REPAIR_WAITING");
    f.env.repairStatus = 3;
    f.Text("BUILDING_REPAIRSTATUS");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_REPAIR_REQUESTED");
    f.env.repairStatus = 7;
    f.Text("BUILDING_REPAIRSTATUS");
    OBL_CHECK(f.out[0] == Char16{0x7777});
    const char* const kClean[] = {
            "BI_CLEAN_NO_CLEANERS", "BI_CLEAN_NOT_NEEDED", "BI_CLEAN_REQUIRED", "BI_CLEAN_ONROUTE", "BI_CLEAN_CLEANING"};
    for (int status = 1; status <= 5; ++status) {
        f.env.cleaningStatus = status;
        f.Text("BUILDING_CLEANINGSTATUS");
        OBL_CHECK_EQ(f.env.lookedUp, kClean[status - 1]);
    }
    f.env.cleaningStatus = 0;
    f.Text("BUILDING_CLEANINGSTATUS");
    OBL_CHECK(f.out[0] == Char16{0x7777});
}

OBL_TEST_CASE("building_text.counters_and_call_state")
{
    Fixture f;
    f.building->m_NumFaults = 4;
    f.building->m_UserRepairInterval = 9;
    f.building->m_NumVisitsToday = -2;
    f.building->m_VisitsSinceCleaned = 31;
    f.Text("BUILDING_NUMFAULTS");
    OBL_CHECK(Text16(f.out) == u"4");
    f.Text("BUILDING_REPAIRINTERVAL");
    OBL_CHECK(Text16(f.out) == u"9");
    f.Text("BUILDING_NUMVISITS");
    OBL_CHECK(Text16(f.out) == u"-2");
    f.Text("BUILDING_SINCECLEANED");
    OBL_CHECK(Text16(f.out) == u"31");
    f.Text("BI_CALL_CLEANER_STATE");
    OBL_CHECK(Text16(f.out) == u"1");
    f.building->m_urgentCleaningCalled = 1;
    f.Text("BI_CALL_CLEANER_STATE");
    OBL_CHECK(Text16(f.out) == u"0");
    f.building->m_urgentRepairCalled = 0;
    f.env.staff = 0;
    f.Text("BI_CALL_MECHANIC_STATE");
    OBL_CHECK(Text16(f.out) == u"0");
    f.env.staff = 2;
    f.Text("BI_CALL_MECHANIC_STATE");
    OBL_CHECK(Text16(f.out) == u"1");
}

OBL_TEST_CASE("building_text.beer_and_commodity_prices")
{
    Fixture f;
    f.def->m_NumCommodities = 3;
    f.def->m_Commodity[0] = 0;
    f.def->m_Commodity[1] = 1; // "Beer"
    f.def->m_Commodity[2] = 2;
    f.building->m_Commod_Price[1] = 4.5f;
    f.building->m_Commod_Cost[1] = 1.25f;
    f.building->m_Commod_Price[0] = 2.0f;
    f.building->m_Commod_Cost[0] = 2.0004f;
    f.building->m_BeerStrength = 0.756f;
    f.Text("BUILDING_BEERCOST");
    OBL_CHECK(Text16(f.out) == u"$1.25");
    f.Text("BUILDING_BEERPRICE");
    OBL_CHECK(Text16(f.out) == u"4.50");
    f.Text("BUILDING_BEERPROFIT$");
    OBL_CHECK(Text16(f.out) == u"$3.25");
    f.Text("BEER_STRENGTH");
    OBL_CHECK(Text16(f.out) == u"0.76");
    f.Text("COMMOD_PRICE_0");
    OBL_CHECK(Text16(f.out) == u"2.00");
    f.Text("COMMOD_PROFIT_1");
    OBL_CHECK(Text16(f.out) == u"3.25");
    // A difference under a tenth of a cent is shown as zero in the money form (and not in the plain one), a NaN as well.
    f.Text("COMMOD_PROFIT$_0");
    OBL_CHECK(Text16(f.out) == u"$0.00");
    f.building->m_Commod_Cost[0] = 1.5f;
    f.Text("COMMOD_PROFIT$_0");
    OBL_CHECK(Text16(f.out) == u"$0.50");
    f.building->m_Commod_Cost[0] = std::numeric_limits<float>::quiet_NaN();
    f.Text("COMMOD_PROFIT$_0");
    OBL_CHECK(Text16(f.out) == u"$0.00");
    // No beer slot: the word before the table (the last intensity word: here the zero-filled neighbour).
    f.env.displayNames[1] = "Cola";
    f.Text("BUILDING_BEERCOST");
    OBL_CHECK(Text16(f.out) == u"$" + obl::text16::Decimal(0) + u".00");
}

OBL_TEST_CASE("building_text.commodity_names")
{
    Fixture f;
    f.def->m_NumCommodities = 3;
    f.def->m_Commodity[0] = 0;
    f.def->m_Commodity[1] = 1;
    f.def->m_Commodity[2] = 2;
    std::strcpy(f.env.beer.m_Brand, "BEER_BRAND");
    std::strcpy(f.env.beer.m_Cocktail, "BEER_COCKTAIL");
    f.Text("COMMOD_NAME_0");
    OBL_CHECK_EQ(f.env.lookedUp, "COMMODITY_NAME_FOOD");
    OBL_CHECK(Text16(f.out) == u"COMMODITY_NAME_FOOD");
    f.def->m_Behavior = BuildingBehavior::Bar;
    f.Text("COMMOD_NAME_1");
    OBL_CHECK_EQ(f.env.lookedUp, "BEER_BRAND");
    OBL_CHECK(Text16(f.out) == u"[BEER_BRAND]");
    f.def->m_Behavior = BuildingBehavior::CocktailBar;
    f.Text("COMMOD_NAME_1");
    OBL_CHECK(Text16(f.out) == u"[BEER_COCKTAIL]");
    f.env.displayNames[1] = "BEER"; // the compare ignores case
    f.Text("COMMOD_NAME_1");
    OBL_CHECK(Text16(f.out) == u"[BEER_COCKTAIL]");
}

OBL_TEST_CASE("building_text.thoughts")
{
    Fixture f;
    object::ThoughtEntry first{const_cast<Char16*>(u"warm"), 7}, second{const_cast<Char16*>(u"crowded"), 9};
    f.building->m_ThoughtList = {&first, &second};
    f.Text("BUILDING_THOUGHTS_0");
    OBL_CHECK(Text16(f.out) == u"[warm]");
    f.Text("BUILDING_THOUGHTS_1");
    OBL_CHECK(Text16(f.out) == u"[crowded]");
    f.Text("THOUGHTS_NAME_1");
    OBL_CHECK(Text16(f.out) == u"[Ann]");
    f.Text("BUILDING_THOUGHTS_");
    OBL_CHECK(Text16(f.out) == u"[warm]"); // no digits: atoi gives 0
    f.Text("BUILDING_THOUGHTS_2");
    OBL_CHECK(Text16(f.out) == u"[]"); // past the list: an empty thought
    f.building->m_ThoughtList.clear();
}

OBL_TEST_CASE("building_text.open_status")
{
    Fixture f;
    f.building->m_OpenStatus = 0;
    f.Text("BI_BUILDING_OPEN");
    OBL_CHECK(Text16(f.out) == u"0");
    f.building->m_OpenStatus = 2;
    f.Text("BI_BUILDING_OPEN");
    OBL_CHECK(Text16(f.out) == u"1");
    f.Text("BI_BUILDING_OPEN_STATUS_5");
    OBL_CHECK(Text16(f.out) == u"1"); // any index but 0
    const auto status = [&](int hour) {
        f.env.hour = hour;
        f.Text("BI_BUILDING_OPEN_STATUS_0");
        return Text16(f.out);
    };
    OBL_CHECK(status(7) == u"0");
    OBL_CHECK(status(8) == u"1");
    OBL_CHECK(status(19) == u"1");
    OBL_CHECK(status(20) == u"0");
    f.env.opening = 22.0f; // wraps midnight, the closing hour itself is open
    f.env.closing = 3.0f;
    OBL_CHECK(status(23) == u"1");
    OBL_CHECK(status(3) == u"1");
    OBL_CHECK(status(4) == u"0");
    OBL_CHECK(status(21) == u"0");
    f.env.opening = f.env.closing = 0.0f; // round the clock
    OBL_CHECK(status(13) == u"1");
    f.building->m_RepairState = 3;
    OBL_CHECK(status(13) == u"0");
    f.building->m_RepairState = 0;
    f.building->m_closedForPower = 1;
    OBL_CHECK(status(13) == u"0");
}

OBL_TEST_CASE("building_text.guest_counters")
{
    Fixture f;
    f.env.world.m_totalGuestArrive[0] = 5;
    f.env.world.m_totalGuestArrive[1] = 7;
    f.env.world.m_todayGuestUnhappyDepart[0] = 2;
    f.env.world.m_todayGuestUnhappyDepart[1] = 3;
    f.Text("BUILDING_GUESTARRTOTAL");
    OBL_CHECK(Text16(f.out) == u"12:5:7");
    f.Text("BUILDING_GUESTUNTODAY");
    OBL_CHECK(Text16(f.out) == u"5:2:3");
    f.Text("BUILDING_GUESTDEPTOTAL");
    OBL_CHECK(Text16(f.out) == u"0:0:0");
}

OBL_TEST_CASE("building_text.demolish_and_construction_progress")
{
    Fixture f;
    f.def->m_RefundValue = 1234;
    f.Text("BUILDING_CONFIRM_DEMOLISH");
    OBL_CHECK_EQ(f.env.lookedUp, "BI_CONFIRM_DEMOLISH");
    f.env.build = 0.375f;
    f.env.demolish = 0.999f;
    f.Text("CONSTRUCT_AMOUNT%");
    OBL_CHECK(Text16(f.out) == u"37%");
    f.Text("DEMOLISH_AMOUNT%");
    OBL_CHECK(Text16(f.out) == u"99%");
    f.Text("CONSTRUCT_AMOUNT");
    OBL_CHECK(Text16(f.out) == u"0.375000");
    f.Text("DEMOLISH_AMOUNT");
    OBL_CHECK(Text16(f.out).substr(0, 5) == u"0.999");
}

OBL_TEST_CASE("building_text.event_slots")
{
    Fixture f;
    auto events = std::make_unique<object::BuildingEvents>();
    f.building->m_events = events.get();
    events->m_slots[1].m_price = 5.5f;
    events->m_slots[1].m_cost = 2.0f;
    events->m_slots[1].m_start = 14.25f;
    f.Text("EVENT1_PRICE");
    OBL_CHECK(Text16(f.out) == u"5.500000");
    f.Text("EVENT1_PROFIT");
    OBL_CHECK(Text16(f.out) == u"3.500000");
    f.Text("EVENT1_START_TIME");
    OBL_CHECK(Text16(f.out) == u"2:14:15"); // slot + 1, hour, quarter minutes
    OBL_CHECK_EQ(f.env.lookedUp, "BI_EVENT_TIME");
    f.Text("EVENT1_PRICEX"); // the price suffix is only a prefix match
    OBL_CHECK(Text16(f.out) == u"5.500000");
    f.Text("EVENT1_PROFITX");
    OBL_CHECK(f.out[0] == Char16{0x7777});
    f.Text("EVENT1_OTHER");
    OBL_CHECK(f.out[0] == Char16{0x7777});
    OBL_CHECK(f.env.fatalKey.empty());
}

OBL_TEST_CASE("building_text.staff_beach_and_unrecognized_key")
{
    Fixture f;
    f.Text("STAFF_BEACH_OPEN");
    OBL_CHECK(Text16(f.out) == u"1");
    f.env.areaOpen[1] = false;
    f.env.areaCalls = 0;
    f.Text("STAFF_BEACH_OPEN");
    OBL_CHECK(Text16(f.out) == u"0");
    f.env.areaOpen[0] = false;
    f.env.areaCalls = 0;
    f.Text("STAFF_BEACH_OPEN");
    OBL_CHECK_EQ(f.env.areaCalls, 1);
    f.Text("STAFF_BEACH_OPEN_STATUS_0");
    OBL_CHECK(Text16(f.out) == u"0");
    f.building->m_numFemalestaff = 2;
    f.Text("STAFF_BEACH_OPEN_STATUS_7");
    OBL_CHECK(Text16(f.out) == u"1");
    f.Text("NO_SUCH_KEY");
    OBL_CHECK_EQ(f.env.fatalKey, "NO_SUCH_KEY");
    OBL_CHECK(f.out[0] == Char16{0x7777});
}
