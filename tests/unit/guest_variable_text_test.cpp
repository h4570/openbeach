#include "core/char16.h"
#include "unit_test.h"
#include "game/objects/guest_variable_text.h"

#include <limits>
#include <memory>
#include <string>

using namespace obl;

namespace {
struct Env {
    Text16 answer;
    std::string lookedUp, fatal;
    Text16 format;
    double real = 0;
    int integer = 0, day = 7;
    bool commodityExists = true;
    const Char16* Lookup(const char* key)
    {
        lookedUp = key;
        answer.assign(lookedUp.begin(), lookedUp.end());
        return answer.c_str();
    }
    const Char16* Text(int) { return u"A thought % text"; }
    const char* DesireName(int i) { return guest_text::kDesires[i]; }
    const char* BuildingKey(int) { return "HOTEL"; }
    bool CurrentCommodity(const object::Guest&, std::string& name)
    {
        name = "MEALS";
        return commodityExists;
    }
    int GameDay() { return day; }
    void Print(Char16*, const Char16* fmt) { format = fmt; }
    void PrintInt(Char16*, const Char16* fmt, int value)
    {
        format = fmt;
        integer = value;
    }
    void PrintReal(Char16*, const Char16* fmt, double value)
    {
        format = fmt;
        real = value;
    }
    void Fatal(const char* key) { fatal = key; }
};
struct Fixture {
    std::unique_ptr<object::Guest> guest = std::make_unique<object::Guest>();
    Env env;
    Char16 out[512] = {};
    void Text(const char* key) { guest_text::GetVariableText(*guest, key, out, env); }
};
} // namespace

OBL_TEST_CASE("guest_text.desire_ties_and_unordered_compare_as_below")
{
    Fixture f;
    for (auto& value : f.guest->m_desires) value = -1;
    OBL_CHECK_EQ(guest_text::StrongestDesire(*f.guest), 0);
    f.guest->m_desires[3] = 1;
    f.guest->m_desires[4] = 1;
    OBL_CHECK_EQ(guest_text::StrongestDesire(*f.guest), 3);
    f.guest->m_desires[24] = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(guest_text::StrongestDesire(*f.guest), 25);
    f.Text("GUEST_DESIRE_0");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_DESIRE_SCARED");
    f.Text("GUEST_DESIRE");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_DESIRE_SCARED");
}

OBL_TEST_CASE("guest_text.activity_priorities_and_missing_building")
{
    Fixture f;
    f.guest->m_activity = 1;
    f.guest->m_desired_commod = 4;
    f.guest->m_state = 41;
    OBL_CHECK_EQ(guest_text::Activity(*f.guest, f.env), "ACTIVITY_LEAVING");
    f.guest->m_state = 40;
    OBL_CHECK_EQ(guest_text::Activity(*f.guest, f.env), "ACTIVITY_ENTERING");
    f.guest->m_state = 0;
    OBL_CHECK_EQ(guest_text::Activity(*f.guest, f.env), "ACTIVITY_FLIRTING");
    f.guest->m_activity = 0;
    OBL_CHECK_EQ(guest_text::Activity(*f.guest, f.env), "ACTIVITY_MEALS");
    f.env.commodityExists = false;
    OBL_CHECK_EQ(guest_text::Activity(*f.guest, f.env), "ACTIVITY_STROLLING");
    f.env.commodityExists = true;
    f.guest->m_state = 58;
    OBL_CHECK_EQ(guest_text::Activity(*f.guest, f.env), "ACTIVITY_STROLLING");
}

OBL_TEST_CASE("guest_text.events_keep_the_fallback_and_building_name")
{
    Fixture f;
    f.guest->m_WorstSpotOpinion = 1;
    f.guest->m_Worst = -2;
    f.Text("GUEST_WORST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_THOUGHT_WORST_NOTHING");
    for (int event : {-1, 4, 5, 11}) {
        f.guest->m_Worst = event;
        f.Text("GUEST_WORST_TODAY");
        OBL_CHECK_EQ(f.env.lookedUp, "GUEST_THOUGHT_BEST_NOTHING");
    }
    f.guest->m_Worst = 10;
    f.Text("GUEST_WORST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_THOUGHT_WORST_DROWNED");
    f.guest->m_WorstSpotOpinion = 0;
    f.Text("GUEST_WORST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "BUILDING_NAME_HOTEL");
    OBL_CHECK(Text16(f.out) == u"BUILDING_NAME_HOTEL");
}

OBL_TEST_CASE("guest_text.clamps_formats_days_and_unrecognized_key")
{
    Fixture f;
    f.guest->m_desires[4] = 12;
    f.Text("GUEST_DESIRE_HEADACHE");
    OBL_CHECK_EQ(f.env.real, 1.0);
    f.guest->m_desires[5] = 12;
    f.Text("GUEST_DESIRE_MEALS");
    OBL_CHECK_EQ(f.env.real, 12.0);
    f.guest->m_sunBurnLevel = -2;
    f.Text("GUEST_SUNBURNT_LEVEL");
    OBL_CHECK_EQ(f.env.real, 1.0);
    f.guest->m_sunBurnLevel = 0.5f;
    f.Text("GUEST_SUNBURNT_LEVEL");
    OBL_CHECK_EQ(f.env.real, 0.0);
    f.guest->m_happiness = 0.759f;
    f.Text("GUEST_HAPPY_PERCENT");
    OBL_CHECK_EQ(f.env.integer, 75);
    OBL_CHECK(f.env.format == u"%d%%");
    f.guest->m_cash = -12.9f;
    f.Text("GUEST_CASH");
    OBL_CHECK_EQ(f.env.integer, -12);
    OBL_CHECK(f.env.format == u"$%d");
    f.guest->m_DayArrived = INT32_MAX;
    f.Text("GUEST_ARRIVED");
    OBL_CHECK_EQ(f.env.integer, INT32_MIN);
    f.guest->m_NightsToStay = 3;
    f.Text("GUEST_DEPART");
    OBL_CHECK_EQ(f.env.integer, 11);
    f.guest->m_Thoughts[1] = 42;
    f.Text("GUEST_THOUGHT_1");
    OBL_CHECK(Text16(f.out) == u"A thought % text");
    f.guest->m_Thoughts[1] = -1;
    f.Text("GUEST_THOUGHT_1");
    OBL_CHECK(Text16(f.out).empty());
    f.Text("guest_name");
    OBL_CHECK_EQ(f.env.fatal, "guest_name");
}
