#pragma once

#include "core/text16.h"
#include "game/tables/building_behavior.h"
#include "game/objects/building_thoughts.h"
#include "game/objects/building_set_variable.h" // BeerSlot
#include "core/ascii_case.h"
#include "core/float_to_int.h"
#include "game/objects/building_events_state.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

// Building::GetVariableText, every key. The Env is the game's side:
//   const wchar_t* Lookup(const char* key)               text lookup of the text table
//   const char* HoursName(int hours)                     WorldParams name of an opening-hours set
//   void PrintInt(wchar_t*, const wchar_t* fmt, int)     the game's own (msvcrt) swprintf
//   void PrintReal(wchar_t*, const wchar_t* fmt, double)
//   void PrintMoney(wchar_t*, const wchar_t* fmt, int dollars, int cents)
//   void PrintInt3(wchar_t*, const wchar_t* fmt, int, int, int)
//   int WholeHour(float) / int QuarterMinutes(float)     the whole hour / the quarter-hour minutes (methods of g_Game)
//   int RepairJobStatus(int id) / int CleaningJobStatus(int id)   repair::repair_facade::GetJobStatus (0..6) / cleaning::cleaning_facade::GetJobStatus (1..5)
//   int CountByClass(int objectClass)                    live objects of that class
//   void PrintString(wchar_t*, const wchar_t* fmt, const wchar_t* text)   swprintf with a "%s" (wide) argument
// const char* CommodityDisplayName(int id) / CommodityName(int id) the commodity display text and script key
//   const tables::BeerDef* Beer(int id)                  beer definition lookup
//   const wchar_t* ObjectName(int32_t objectId)          the object's m_name (a guest's name; the id must exist, nothing checks it)
// int Hour / float OpeningStart(int set) / float OpeningEnd(int set) the world's hour / open and close hour of an opening-hours set
//   const auto& GameWorld()                     the guest counters read by the BUILDING_GUEST* keys
//   float BuildProgress(int id) / float DemolishProgress(int id)   construction and demolition progress in the range 0..1
//   bool AreaOpen(int kind, int area)                    the area opening flag
//   void Fatal(const char* key)                          FatalError("A script read unrecognized building variable \"%s\"") (does not return)
namespace obl::building_text {

inline bool HasPrefix(const char* key, const char* prefix)
{
    return std::strncmp(key, prefix, std::strlen(prefix)) == 0;
}

// A money amount prints as dollars = trunc(ceil(|v| * 100)) / 100 and cents = round(frac(v) * 100) (round to nearest even, float
// product), a NaN counts as negative (an unordered compare reads as below), and the printed text is cut three characters after its last '.'.
inline int RoundToInt(float value)
{
    if (!(value > -2147483648.0f && value < 2147483648.0f)) return INT_MIN; // the "integer indefinite" value
    return static_cast<int>(std::nearbyint(value));
}

inline int AbsInt(int value)
{
    return value < 0 ? static_cast<int>(0u - static_cast<unsigned>(value)) : value;
}

template<class Env> void MoneyText(float value, Char16* out, Env& env)
{
    double whole;
    const float fraction = static_cast<float>(std::modf(static_cast<double>(value), &whole));
    const bool negative = !(value >= 0.0f);
    const int dollars = FloatToIntWrap(std::ceil(std::fabs(static_cast<double>(value)) * 100.0)) / 100;
    const float scaled = fraction * 100.0f;
    const int cents = RoundToInt(scaled);
    if (negative)
        env.PrintMoney(out, u"-$%d.%02d", AbsInt(dollars), AbsInt(cents));
    else
        env.PrintMoney(out, u"$%d.%02d", dollars, AbsInt(cents));
    Char16* point = text16::FindLast(out, u'.');
    if (point[3] != u'\0') point[3] = u'\0';
}

// The text key of the opening-hours set, or nullptr for any other name.
inline const char* HoursKey(const char* name)
{
    if (ascii::EqualsNoCase(name, "ALLHOURS")) return "BI_HOURS_ALLDAY";
    if (ascii::EqualsNoCase(name, "MORNING")) return "BI_HOURS_MORNING";
    if (ascii::EqualsNoCase(name, "DAY")) return "BI_HOURS_DAY";
    if (ascii::EqualsNoCase(name, "EVENING")) return "BI_HOURS_EVENING";
    if (ascii::EqualsNoCase(name, "NIGHT")) return "BI_HOURS_NIGHT";
    return nullptr;
}

// A "time of the last fault/inspection" line: a time below zero (or NaN, which also counts as "below") means "never". The
// format (the fault one for both) gets the whole hour, the quarter minutes and the day + 1.
template<class Env> void LastEventText(float time, int day, const char* noneKey, Char16* out, Env& env)
{
    if (!(time >= 0.0f)) {
        env.PrintInt(out, env.Lookup(noneKey), 0);
        return;
    }
    const int quarter = env.QuarterMinutes(time);
    const int hour = env.WholeHour(time);
    env.PrintInt3(out, env.Lookup("BI_LAST_FAULT_FORMAT"), hour, quarter, day + 1);
}

// The "called the cleaner/mechanic" probe: 1 only when staff of that class exists and no call is pending, otherwise 0.
template<class Env> void CallStateText(uint8_t called, int objectClass, Char16* out, Env& env)
{
    env.PrintInt(out, u"%d", env.CountByClass(objectClass) != 0 && called == 0 ? 1 : 0);
}

// The beer keys address the slot that sells beer (-1 when none: the word before the table); COMMOD_* take the slot from the key.
inline float PriceAt(const object::Building& building, int slot)
{
    return (&building.m_Commod_Price[0])[slot];
}
inline float CostAt(const object::Building& building, int slot)
{
    return (&building.m_Commod_Cost[0])[slot];
}

// Price minus cost as computed at 53 bits: both operands are floats, so the double difference is the same.
inline double ProfitAt(const object::Building& building, int slot)
{
    return static_cast<double>(PriceAt(building, slot)) - static_cast<double>(CostAt(building, slot));
}

// COMMOD_NAME_<n>: a beer slot (display text "Beer") shows the brand, or the cocktail when the object's own name is COCKTAIL; any other commodity
// looks up COMMODITY_NAME_<key>.
template<class Env> void CommodityNameText(const object::Building& building, int slot, Char16* out, Env& env)
{
    const int id = building.m_definition->m_Commodity[slot];
    if (ascii::EqualsNoCase("Beer", env.CommodityDisplayName(id))) {
        const tables::BeerDef* beer = env.Beer(building.m_BeerType);
        const char* beerKey = building.m_definition->m_Behavior == BuildingBehavior::CocktailBar ? beer->m_Cocktail : beer->m_Brand;
        env.PrintString(out, u"%s", env.Lookup(beerKey));
        return;
    }
    std::string textKey = "COMMODITY_NAME_";
    textKey += env.CommodityName(id);
    text16::Copy(out, env.Lookup(textKey.c_str()));
}

// Thought `index` of the list; an index past either end reads an empty thought of object 0 instead of memory
// outside the list.
inline const object::ThoughtEntry& ThoughtAt(const object::Building& building, int index)
{
    static Char16 empty[1] = {};
    static const object::ThoughtEntry kNone{empty, 0};
    const object::ThoughtEntry* entry = thought_ops::At(building.m_ThoughtList, index);
    return entry && entry->m_text ? *entry : kNone;
}

// BI_BUILDING_OPEN_STATUS_<n>: 1 for any n other than 0; for n = 0 whether the building is inside its opening hours and not shut by a power cut or a
// breakdown (m_RepairState 3). The branches compare whole hours unsigned.
template<class Env> int OpenStatusAt(const object::Building& building, int index, Env& env)
{
    if (index != 0) return 1;
    if (building.m_RepairState == 3 || building.m_closedForPower != 0) return 0;
    const uint32_t hour = static_cast<uint32_t>(env.Hour());
    const int set = building.m_definition->m_OpeningHours;
    const uint32_t start = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(env.OpeningStart(set))));
    const uint32_t end = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(env.OpeningEnd(set))));
    if (hour >= start && hour < end) return 1;
    if (start <= end) return start == end ? 1 : 0; // equal: round the clock
    if (hour >= start) return 1; // wraps midnight
    return hour > end ? 0 : 1;
}

// EVENT<digit>_PRICE / _PROFIT / _START_TIME: the digit picks a slot of the building's schedule with no range check
// (the slot is indexed straight from the start of the schedule).
inline const object::EventSlot& EventSlotAt(const object::Building& building, int slot)
{
    return (&building.m_events->m_slots[0])[slot];
}

template<class Env> void EventText(const object::Building& building, const char* key, Char16* out, Env& env)
{
    const int slot = key[5] - '0';
    const char* what = key + 6;
    const object::EventSlot& event = EventSlotAt(building, slot);
    if (std::strncmp(what, "_PRICE", 6) == 0) {
        env.PrintReal(out, u"%f", static_cast<double>(event.m_price));
    } else if (std::strcmp(what, "_PROFIT") == 0) {
        env.PrintReal(out, u"%f", static_cast<double>(event.m_price) - static_cast<double>(event.m_cost));
    } else if (std::strcmp(what, "_START_TIME") == 0) {
        const int quarter = env.QuarterMinutes(event.m_start);
        const int hour = env.WholeHour(event.m_start);
        env.PrintInt3(out, env.Lookup("BI_EVENT_TIME"), slot + 1, hour, quarter);
    } // any other suffix leaves the buffer untouched
}

// "total, first, second" of a pair of world counters.
template<class Env> void GuestCounterText(const int (&pair)[2], Char16* out, Env& env)
{
    const int sum = static_cast<int>(static_cast<uint32_t>(pair[0]) + static_cast<uint32_t>(pair[1]));
    env.PrintInt3(out, env.Lookup("FP_GUEST_FORMAT"), sum, pair[0], pair[1]);
}

// A key may leave the buffer untouched (BUILDING_OPEN with a status above 1); an unknown key is a fatal error.
template<class Env> void GetVariableText(const object::Building& building, const char* key, Char16* out, Env& env)
{
    if (std::strcmp(key, "BUILDING_NAME") == 0) {
        std::string textKey = "BUILDING_NAME_";
        textKey += building.m_definition->m_key;
        text16::Copy(out, env.Lookup(textKey.c_str()));
    } else if (std::strcmp(key, "BUILDING_OPEN") == 0) {
        if (building.m_OpenStatus <= 1) text16::Copy(out, env.Lookup(building.m_OpenStatus == 0 ? "OPEN" : "CLOSED"));
    } else if (std::strcmp(key, "BUILDING_OPENHOURS") == 0) {
        const char* textKey = HoursKey(env.HoursName(building.m_definition->m_OpeningHours));
        if (textKey)
            text16::Copy(out, env.Lookup(textKey));
        else
            text16::Copy(out, u"HOURS KEYWORD NOT RECOGNISED");
    } else if (std::strcmp(key, "BUILDING_MAXGUESTS") == 0) {
        env.PrintInt(out, u"%d", building.m_definition->m_GuestCapacity);
    } else if (std::strcmp(key, "BUILDING_CURRENTGUESTS") == 0) {
        env.PrintInt(out, u"%d", building.m_NumGuests);
    } else if (std::strcmp(key, "BUILDING_SATISFACTION") == 0) {
        env.PrintReal(out, u"%.2f", static_cast<double>(building.m_SatisfactionPercentage));
    } else if (std::strcmp(key, "BUILDING_INCOMETODAY$") == 0) {
        MoneyText(building.m_incomeToday, out, env);
    } else if (std::strcmp(key, "BUILDING_COSTTODAY$") == 0) {
        MoneyText(building.m_costToday, out, env);
    } else if (std::strcmp(key, "BUILDING_PROFITTODAY$") == 0) {
        MoneyText(building.m_profitToday, out, env);
    } else if (std::strcmp(key, "BUILDING_AVERAGEPROFIT$") == 0) {
        MoneyText(building.m_averageProfit, out, env);
    } else if (std::strcmp(key, "BUILDING_POWER") == 0) {
        const int power = static_cast<int>(building.m_PowerConsumption); // printed signed although the field is unsigned
        env.PrintInt(out, u"%d", power >= 0 ? power : (building.m_OpenStatus == 0 ? AbsInt(power) : 0));
    } else if (std::strcmp(key, "BUILDING_OUTPUT_POWER") == 0) {
        const int power = building.m_definition->m_Power;
        int shown = power;
        if (power < 0) {
            shown = 0;
            if (building.m_OpenStatus == 0) {
                // The wear state scales a generator's output (good .. broken: 1, .75, .5, .25); a state beyond them shows it unscaled.
                static const double kScale[] = {1.0, 0.75, 0.5, 0.25};
                const int magnitude = AbsInt(power);
                shown = building.m_RepairState <= 3 ? FloatToIntWrap(static_cast<double>(magnitude) * kScale[building.m_RepairState])
                                                    : magnitude;
            }
        }
        env.PrintInt(out, u"%d", shown);
    } else if (std::strcmp(key, "BUILDING_POWER_TITLE") == 0) {
        text16::Copy(out, env.Lookup(static_cast<int>(building.m_PowerConsumption) >= 0 ? "BI_POWER" : "BI_POWER_OUTPUT"));
    } else if (std::strcmp(key, "BUILDING_AGE") == 0) {
        if (building.m_BuildingAge == 0)
            env.PrintInt(out, env.Lookup("BI_BUILD_AGE_NEW"), 0);
        else if (building.m_BuildingAge == 1)
            env.PrintInt(out, env.Lookup("BI_BUILD_AGE_1"), 0);
        else
            env.PrintInt(out, env.Lookup("BI_BUILD_AGE_N"), building.m_BuildingAge);
    } else if (std::strcmp(key, "BUILDING_LAST_FAULT") == 0) {
        LastEventText(building.m_lastFaultDayTime, building.m_LastFaultDay, "BI_LAST_FAULT_NONE", out, env);
    } else if (std::strcmp(key, "BUILDING_LASTINSPECT") == 0) {
        LastEventText(building.m_lastRepairDayTime, building.m_LastRepairDay, "BI_LAST_INSPECT_NONE", out, env);
    } else if (std::strcmp(key, "BUILDING_NUMFAULTS") == 0) {
        env.PrintInt(out, u"%d", building.m_NumFaults);
    } else if (std::strcmp(key, "BUILDING_REPAIRSTATE") == 0) {
        static const char* const kNames[] = {"GOOD", "POOR", "DANGEROUS", "BROKEN"};
        if (building.m_RepairState <= 3) text16::Copy(out, env.Lookup(kNames[building.m_RepairState]));
    } else if (std::strcmp(key, "BUILDING_REPAIRSTATUS") == 0) {
        const bool idle = building.m_calledRepairing == 0 && building.m_urgentRepairCalled == 0;
        const char* textKey = nullptr;
        switch (static_cast<unsigned>(env.RepairJobStatus(static_cast<int>(building.m_id)))) {
        case 0: textKey = "BI_REPAIR_NO_MECHANICS"; break;
        case 1: textKey = "BI_REPAIR_CANT_REACH"; break;
        case 2: textKey = idle ? "BI_REPAIR_NOT_NEEDED" : "BI_REPAIR_WAITING"; break;
        case 3: textKey = idle ? "BI_REPAIR_REQUIRED" : "BI_REPAIR_REQUESTED"; break;
        case 4: textKey = "BI_INSPECT_REQUIRED"; break;
        case 5: textKey = "BI_REPAIR_MECHANIC_COMING"; break;
        case 6: textKey = "BI_REPAIR_REPAIRING"; break;
        default: break;
        }
        if (textKey) text16::Copy(out, env.Lookup(textKey));
    } else if (std::strcmp(key, "BUILDING_CLEANINGSTATUS") == 0) {
        const char* textKey = nullptr;
        switch (env.CleaningJobStatus(static_cast<int>(building.m_id))) {
        case 1: textKey = "BI_CLEAN_NO_CLEANERS"; break;
        case 2: textKey = "BI_CLEAN_NOT_NEEDED"; break;
        case 3: textKey = "BI_CLEAN_REQUIRED"; break;
        case 4: textKey = "BI_CLEAN_ONROUTE"; break;
        case 5: textKey = "BI_CLEAN_CLEANING"; break;
        default: break;
        }
        if (textKey) text16::Copy(out, env.Lookup(textKey));
    } else if (std::strcmp(key, "BUILDING_REPAIRINTERVAL") == 0) {
        env.PrintInt(out, u"%d", building.m_UserRepairInterval);
    } else if (std::strcmp(key, "BUILDING_NUMVISITS") == 0) {
        env.PrintInt(out, u"%d", building.m_NumVisitsToday);
    } else if (std::strcmp(key, "BUILDING_SINCECLEANED") == 0) {
        env.PrintInt(out, u"%d", building.m_VisitsSinceCleaned);
    } else if (std::strcmp(key, "BI_CALL_CLEANER_STATE") == 0) {
        CallStateText(building.m_urgentCleaningCalled, 0xb, out, env);
    } else if (std::strcmp(key, "BI_CALL_MECHANIC_STATE") == 0) {
        CallStateText(building.m_urgentRepairCalled, 8, out, env);
    } else if (std::strcmp(key, "BUILDING_BEERCOST") == 0) {
        env.PrintReal(out, u"$%.2f", static_cast<double>(CostAt(building, building_set_variable::BeerSlot(building, env))));
    } else if (std::strcmp(key, "BUILDING_BEERPROFIT$") == 0) {
        env.PrintReal(out, u"$%.2f", ProfitAt(building, building_set_variable::BeerSlot(building, env)));
    } else if (std::strcmp(key, "BUILDING_BEERPRICE") == 0) {
        env.PrintReal(out, u"%.2f", static_cast<double>(PriceAt(building, building_set_variable::BeerSlot(building, env))));
    } else if (HasPrefix(key, "BUILDING_THOUGHTS_")) {
        env.PrintString(out, u"%s", ThoughtAt(building, std::atoi(key + 18)).m_text);
    } else if (HasPrefix(key, "THOUGHTS_NAME_")) {
        env.PrintString(out, u"%s", env.ObjectName(ThoughtAt(building, std::atoi(key + 14)).m_objectId));
    } else if (HasPrefix(key, "COMMOD_PRICE_")) {
        env.PrintReal(out, u"%.2f", static_cast<double>(PriceAt(building, std::atoi(key + 13))));
    } else if (HasPrefix(key, "COMMOD_NAME_")) {
        CommodityNameText(building, std::atoi(key + 12), out, env);
    } else if (HasPrefix(key, "COMMOD_PROFIT_")) {
        env.PrintReal(out, u"%.2f", ProfitAt(building, std::atoi(key + 14)));
    } else if (HasPrefix(key, "COMMOD_PROFIT$_")) {
        // A difference within a tenth of a cent (or NaN: it takes the same path) shows as zero.
        double profit = ProfitAt(building, std::atoi(key + 15));
        if (!(std::fabs(profit) > static_cast<double>(0.001f))) profit = 0.0;
        env.PrintReal(out, u"$%.2f", profit);
    } else if (std::strcmp(key, "BEER_STRENGTH") == 0) {
        env.PrintReal(out, u"%.2f", static_cast<double>(building.m_BeerStrength));
    } else if (std::strcmp(key, "BI_BUILDING_OPEN") == 0) {
        env.PrintInt(out, u"%d", building.m_OpenStatus == 0 ? 0 : 1);
    } else if (HasPrefix(key, "BI_BUILDING_OPEN_STATUS_")) {
        env.PrintInt(out, u"%d", OpenStatusAt(building, std::atoi(key + 24), env));
    } else if (std::strcmp(key, "BUILDING_GUESTARRTOTAL") == 0) {
        GuestCounterText(env.GameWorld().m_totalGuestArrive, out, env);
    } else if (std::strcmp(key, "BUILDING_GUESTARRTODAY") == 0) {
        GuestCounterText(env.GameWorld().m_todayGuestArrive, out, env);
    } else if (std::strcmp(key, "BUILDING_GUESTDEPTOTAL") == 0) {
        GuestCounterText(env.GameWorld().m_totalGuestDepart, out, env);
    } else if (std::strcmp(key, "BUILDING_GUESTDEPTODAY") == 0) {
        GuestCounterText(env.GameWorld().m_todayGuestDepart, out, env);
    } else if (std::strcmp(key, "BUILDING_GUESTUNTOTAL") == 0) {
        GuestCounterText(env.GameWorld().m_totalGuestUnhappyDepart, out, env);
    } else if (std::strcmp(key, "BUILDING_GUESTUNTODAY") == 0) {
        GuestCounterText(env.GameWorld().m_todayGuestUnhappyDepart, out, env);
    } else if (std::strcmp(key, "BUILDING_CONFIRM_DEMOLISH") == 0) {
        env.PrintInt(out, env.Lookup("BI_CONFIRM_DEMOLISH"), building.m_definition->m_RefundValue);
    } else if (std::strcmp(key, "CONSTRUCT_AMOUNT%") == 0) {
        env.PrintInt(out, u"%d%%", FloatToIntWrap(static_cast<double>(env.BuildProgress(building.m_id)) * 100.0));
    } else if (std::strcmp(key, "CONSTRUCT_AMOUNT") == 0) {
        env.PrintReal(out, u"%f", static_cast<double>(env.BuildProgress(building.m_id)));
    } else if (std::strcmp(key, "DEMOLISH_AMOUNT%") == 0) {
        env.PrintInt(out, u"%d%%", FloatToIntWrap(static_cast<double>(env.DemolishProgress(building.m_id)) * 100.0));
    } else if (std::strcmp(key, "DEMOLISH_AMOUNT") == 0) {
        env.PrintReal(out, u"%f", static_cast<double>(env.DemolishProgress(building.m_id)));
    } else if (HasPrefix(key, "EVENT")) {
        EventText(building, key, out, env);
    } else if (std::strcmp(key, "STAFF_BEACH_OPEN") == 0) {
        const bool open = env.AreaOpen(0, building.m_InfluenceArea) && env.AreaOpen(1, building.m_InfluenceArea);
        env.PrintInt(out, u"%d", open ? 1 : 0);
    } else if (HasPrefix(key, "STAFF_BEACH_OPEN_STATUS_")) {
        env.PrintInt(out, u"%d", building.m_numMalestaff + building.m_numFemalestaff != 0 ? 1 : 0);
    } else {
        env.Fatal(key);
    }
}

} // namespace obl::building_text
