#pragma once

#include "core/text16.h"
#include "core/float_to_int.h"
#include "game/objects/object_guest.h"

#include <cstring>
#include <cwchar>
#include <string>

namespace obl::guest_text {

inline constexpr const char* kDesires[] = {"ACCOMDTN", "ALCOHOL", "AMUSEMENT", "FIRSTAID", "HEADACHE", "MEALS", "PARTYFUN", "SHOWER",
        "SNACKS", "SOFTDRNK", "SOUVENIR", "SPORTFUN", "SUNBATHE", "SWIMFUN", "RELEIF", "WATERFUN", "STROLL", "LEAVE", "VANDAL", "FIGHT",
        "FLIRT", "UMBRELS", "SUNTAN", "HARDSEX", "SLEEP", "SCARED"};
inline constexpr const char* kOpinions[] = {"BEAUTY", "CROWDING", "TOTTY", "PRICE", "VARIETY", "CLEANLINESS", "SAFETY"};

inline int StrongestDesire(const object::Guest& guest)
{
    float highest = 0;
    int index = 0;
    for (int i = 0; i < 26; ++i) {
        // The compare also accepts unordered values; the next comparison replaces a NaN.
        if (!(highest >= guest.m_desires[i])) {
            highest = guest.m_desires[i];
            index = i;
        }
    }
    return index;
}

template<class Env> std::string Activity(const object::Guest& guest, Env& env)
{
    if (guest.m_state == 41) return "ACTIVITY_LEAVING";
    if (guest.m_state == 40) return "ACTIVITY_ENTERING";
    if (!guest.m_activity) {
        if (guest.m_state == 12 || guest.m_state == 58) return "ACTIVITY_STROLLING";
        std::string commodity;
        if (!env.CurrentCommodity(guest, commodity)) return "ACTIVITY_STROLLING";
        return "ACTIVITY_" + commodity;
    }
    switch (guest.m_desired_commod) {
    case 0: return "ACTIVITY_STROLLING";
    case 1: return "ACTIVITY_SUNBATHING";
    case 2: return "ACTIVITY_SWIMMING";
    case 3: return "ACTIVITY_FIGHTING";
    case 4: return "ACTIVITY_FLIRTING";
    case 6: return "ACTIVITY_PEEING";
    default: return "ACTIVITY_UNKNOWN";
    }
}

template<class Env> void VisitText(const object::Guest& guest, bool best, Char16* out, Env& env)
{
    const int event = best ? guest.m_Best : guest.m_Worst;
    const bool spot = (best ? guest.m_BestSpotOpinion : guest.m_WorstSpotOpinion) != 0;
    const char* key = "GUEST_THOUGHT_BEST_NOTHING";
    if (event == -2)
        key = best ? "GUEST_THOUGHT_BEST_NOTHING" : "GUEST_THOUGHT_WORST_NOTHING";
    else if (!spot) {
        const std::string building = "BUILDING_NAME_" + std::string(env.BuildingKey(best ? guest.m_BestType : guest.m_WorstType));
        text16::Copy(out, env.Lookup(building.c_str()));
        return;
    } else if (best) {
        if (event == 4) key = "GUEST_THOUGHT_BEST_TOPLESS";
        if (event == 5) key = "GUEST_THOUGHT_BEST_WETTSHIRT";
    } else {
        static constexpr const char* kWorst[] = {"GUEST_THOUGHT_WORST_STINKY_TOILET", "GUEST_THOUGHT_WORST_VOMIT",
                "GUEST_THOUGHT_WORST_VANDALISM", "GUEST_THOUGHT_WORST_FIGHTING", "GUEST_THOUGHT_BEST_NOTHING", "GUEST_THOUGHT_BEST_NOTHING",
                "GUEST_THOUGHT_WORST_QUEUE_CLOSED", "GUEST_THOUGHT_WORST_QUEUE_BROKEN", "GUEST_THOUGHT_WORST_EATEN",
                "GUEST_THOUGHT_WORST_SHARK", "GUEST_THOUGHT_WORST_DROWNED"};
        if (static_cast<uint32_t>(event) < 11) key = kWorst[event];
    }
    // These translations are used as formats, even though there are no arguments.
    env.Print(out, env.Lookup(key));
}

template<class Env> void GetVariableText(const object::Guest& guest, const char* key, Char16* out, Env& env)
{
    const auto is = [&](const char* name) { return std::strcmp(key, name) == 0; };
    const auto real = [&](double value) { env.PrintReal(out, u"%.2f", value); };
    if (is("GUEST_NAME")) {
        text16::Copy(out, guest.m_name);
        return;
    }
    if (is("GUEST_CURRENTACTIVITY")) {
        const auto activity = Activity(guest, env);
        text16::Copy(out, env.Lookup(activity.c_str()));
        return;
    }
    if (is("GUEST_DESIRE") || is("GUEST_DESIRE_0")) {
        const std::string desire = "GUEST_DESIRE_" + std::string(env.DesireName(StrongestDesire(guest)));
        text16::Copy(out, env.Lookup(desire.c_str()));
        return;
    }
    for (int i = 0; i < 3; ++i) {
        const std::string thought = "GUEST_THOUGHT_" + std::to_string(i);
        if (is(thought.c_str())) {
            text16::Copy(out, guest.m_Thoughts[i] == -1 ? u"" : env.Text(guest.m_Thoughts[i]));
            return;
        }
    }
    if (is("GUEST_BEST_TODAY") || is("GUEST_WORST_TODAY")) {
        VisitText(guest, is("GUEST_BEST_TODAY"), out, env);
        return;
    }
    for (int i = 0; i < 26; ++i) {
        const std::string desire = "GUEST_DESIRE_" + std::string(kDesires[i]);
        if (is(desire.c_str())) {
            double value = guest.m_desires[i];
            if (i == 4 && value > 1) value = 1;
            real(value);
            return;
        }
    }
    for (int i = 0; i < 7; ++i) {
        const std::string opinion = "GUEST_OPINION_" + std::string(kOpinions[i]);
        if (is(opinion.c_str())) {
            real(guest.m_opinion[i]);
            return;
        }
    }
    if (is("GUEST_OPINION_OVERALL")) {
        // The sum stays in double precision and is multiplied it by the single-precision 1/7 constant.
        double total = 0;
        for (float value : guest.m_opinion) total += value;
        real(total * static_cast<double>(0.14285715f));
        return;
    }
    if (is("GUEST_ILL")) {
        real(guest.m_foodIll);
        return;
    }
    if (is("GUEST_DRUNK") || is("GUEST_DRUNK_LEVEL")) {
        real(guest.m_drunk);
        return;
    }
    if (is("GUEST_SUNBURNT_LEVEL")) {
        double value = guest.m_sunBurnLevel >= 0 ? 0 : -static_cast<double>(guest.m_sunBurnLevel);
        real(value > 1 ? 1 : value);
        return;
    }
    if (is("GUEST_OXYGEN_LEVEL")) {
        env.PrintInt(out, u"%d", guest.m_oxygen);
        return;
    }
    if (is("GUEST_HAPPY_PERCENT")) {
        env.PrintInt(out, u"%d%%", FloatToIntWrap(static_cast<double>(guest.m_happiness) * 100));
        return;
    }
    if (is("GUEST_HAPPY_VALUE")) {
        real(guest.m_happiness);
        return;
    }
    if (is("GUEST_CASH")) {
        env.PrintInt(out, u"$%d", FloatToIntWrap(guest.m_cash));
        return;
    }
    if (is("GUEST_ARRIVED")) {
        env.PrintInt(out, env.Lookup("STAFF_DAY_TEXT"), static_cast<int32_t>(static_cast<uint32_t>(guest.m_DayArrived) + 1));
        return;
    }
    if (is("GUEST_DEPART")) {
        env.PrintInt(out, env.Lookup("STAFF_DAY_TEXT"),
                static_cast<int32_t>(static_cast<uint32_t>(guest.m_NightsToStay) + 1 + static_cast<uint32_t>(env.GameDay())));
        return;
    }
    env.Fatal(key);
}

} // namespace obl::guest_text
