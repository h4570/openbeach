// Guest variable text (src/game/objects/guest_variable_text.h): the activity table by desired commodity, the best-visit
// thoughts and the overall opinion, which guest_variable_text_test.cpp leaves out.

#include "core/char16.h"
#include "unit_test.h"
#include "game/objects/guest_variable_text.h"

#include <memory>
#include <string>

using namespace obl;

namespace {
struct Env {
    Text16 answer;
    std::string lookedUp;
    double real = 0;
    Text16 format;
    const Char16* Lookup(const char* key)
    {
        lookedUp = key;
        answer.assign(lookedUp.begin(), lookedUp.end());
        return answer.c_str();
    }
    const Char16* Text(int) { return u""; }
    const char* DesireName(int i) { return guest_text::kDesires[i]; }
    const char* BuildingKey(int) { return "HOTEL"; }
    bool CurrentCommodity(const object::Guest&, std::string& name)
    {
        name = "MEALS";
        return true;
    }
    int GameDay() { return 0; }
    void Print(Char16*, const Char16* fmt) { format = fmt; }
    void PrintInt(Char16*, const Char16* fmt, int) { format = fmt; }
    void PrintReal(Char16*, const Char16* fmt, double value)
    {
        format = fmt;
        real = value;
    }
    void Fatal(const char*) {}
};
struct Fixture {
    std::unique_ptr<object::Guest> guest = std::make_unique<object::Guest>();
    Env env;
    Char16 out[512] = {};
    void Text(const char* key) { guest_text::GetVariableText(*guest, key, out, env); }
};
} // namespace

OBL_TEST_CASE("guest_text_more.activity_follows_the_desired_commodity")
{
    Fixture f;
    f.guest->m_activity = 1;
    f.guest->m_state = 0;
    struct Row {
        int commodity;
        const char* name;
    };
    const Row rows[] = {{0, "ACTIVITY_STROLLING"}, {1, "ACTIVITY_SUNBATHING"}, {2, "ACTIVITY_SWIMMING"}, {3, "ACTIVITY_FIGHTING"},
            {4, "ACTIVITY_FLIRTING"}, {6, "ACTIVITY_PEEING"},
            // 5 (and everything else) has no name in the table
            {5, "ACTIVITY_UNKNOWN"}, {7, "ACTIVITY_UNKNOWN"}, {-1, "ACTIVITY_UNKNOWN"}};
    for (const Row& row : rows) {
        f.guest->m_desired_commod = row.commodity;
        OBL_CHECK_EQ(guest_text::Activity(*f.guest, f.env), std::string(row.name));
    }
    // GUEST_CURRENTACTIVITY goes through the same table and the translation lookup.
    f.guest->m_desired_commod = 2;
    f.Text("GUEST_CURRENTACTIVITY");
    OBL_CHECK_EQ(f.env.lookedUp, "ACTIVITY_SWIMMING");
}

OBL_TEST_CASE("guest_text_more.best_visit_thoughts")
{
    Fixture f;
    f.guest->m_BestSpotOpinion = 1;
    f.guest->m_Best = -2;
    f.Text("GUEST_BEST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_THOUGHT_BEST_NOTHING");
    f.guest->m_Best = 4;
    f.Text("GUEST_BEST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_THOUGHT_BEST_TOPLESS");
    f.guest->m_Best = 5;
    f.Text("GUEST_BEST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_THOUGHT_BEST_WETTSHIRT");
    // Any other event keeps the default "nothing" text for the best visit.
    f.guest->m_Best = 7;
    f.Text("GUEST_BEST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "GUEST_THOUGHT_BEST_NOTHING");
    // Without a spot opinion the building name is shown, from the best type (not the worst one).
    f.guest->m_BestSpotOpinion = 0;
    f.Text("GUEST_BEST_TODAY");
    OBL_CHECK_EQ(f.env.lookedUp, "BUILDING_NAME_HOTEL");
}

OBL_TEST_CASE("guest_text_more.overall_opinion_is_the_single_precision_mean")
{
    Fixture f;
    for (int i = 0; i < 7; ++i) f.guest->m_opinion[i] = static_cast<float>(i); // sum 21
    f.Text("GUEST_OPINION_OVERALL");
    OBL_CHECK_EQ(f.env.real, 21.0 * static_cast<double>(0.14285715f));
    OBL_CHECK(f.env.format == u"%.2f");
    for (float& value : f.guest->m_opinion) value = 0.5f;
    f.Text("GUEST_OPINION_OVERALL");
    OBL_CHECK_EQ(f.env.real, 3.5 * static_cast<double>(0.14285715f));
}
