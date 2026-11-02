// FeedbackManager logic (src/game/feedback/feedback_logic.h) on the native class with a scripted world and GUI.

#include "unit_test.h"

#include "game/feedback/feedback_logic.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl::feedback;

namespace {

// One balloon the GUI was asked for.
struct GuiCall {
    bool key = false;
    std::string keyText;
    std::u16string text;
    int mode = 0;
};

// The world clock, the balloon, the RNG and the complaint counters under the test's control.
struct FakeEnv {
    int day = 4;
    float hour = 10.0f;
    bool canShow = true; // the balloon: free until something is shown
    uint32_t counts[kFamilies] = {};
    std::vector<int> randoms;
    int draws = 0;
    std::vector<GuiCall> calls;

    int GameDay() { return day; }
    float GameHour() { return hour; }
    bool CanShowMessage() { return canShow; }
    void ShowMessage(const char16_t* text, int mode)
    {
        calls.push_back({false, {}, text, mode});
        canShow = false;
    }
    void ShowMessageKey(const char* key, int mode)
    {
        calls.push_back({true, key, {}, mode});
        canShow = false;
    }
    int Rand() { return static_cast<size_t>(draws) < randoms.size() ? randoms[draws++] : (++draws, 0); }
    uint32_t* FamilyCounts() { return counts; }
};

struct Fixture {
    Manager m;
    FakeEnv env;
    Fixture() { m.lastReviewHour = 10.0f; }
};

} // namespace

OBL_TEST_CASE("feedback.state starts empty with the review clock at 25")
{
    Manager m;
    OBL_CHECK(m.pending.empty());
    OBL_CHECK(m.history.empty());
    OBL_CHECK_EQ(m.historyCursor, size_t(0));
    OBL_CHECK_EQ(m.lastReviewHour, 25.0f);
    OBL_CHECK_EQ(m.suppressedCount, 0u);
    OBL_CHECK_EQ(m.suppressed[99], 0u);
    OBL_CHECK_EQ(int(m.suppressWorld), 0);
}

OBL_TEST_CASE("feedback.add shows at once when the balloon is free")
{
    Fixture f;
    Add(f.m, f.env, "FEEDBACK_TOILET", 7, 5);
    OBL_REQUIRE(f.env.calls.size() == 1);
    OBL_CHECK(f.env.calls[0].key);
    OBL_CHECK_EQ(f.env.calls[0].keyText, std::string("FEEDBACK_TOILET"));
    OBL_CHECK_EQ(f.env.calls[0].mode, 2);
    OBL_CHECK(f.m.pending.empty());
    OBL_REQUIRE(f.m.history.size() == 1);
    OBL_CHECK_EQ(f.m.history[0].objectId, 7);
    OBL_CHECK_EQ(f.m.history[0].day, 4);
    OBL_CHECK_EQ(f.m.history[0].hour, 10.0f);
    OBL_CHECK_EQ(f.m.history[0].text.key, std::string("FEEDBACK_TOILET"));
    OBL_CHECK(!f.m.history[0].text.wide);
}

OBL_TEST_CASE("feedback.add queues while the balloon is busy and keeps UTF-16 texts")
{
    Fixture f;
    f.env.canShow = false;
    AddWide(f.m, f.env, u"Pompa zepsuta ż\U0001F30A", 3, 4);
    Add(f.m, f.env, "FEEDBACK_SHOWER", -2, 5);
    OBL_CHECK(f.env.calls.empty());
    OBL_REQUIRE(f.m.pending.size() == 2);
    OBL_CHECK(f.m.pending[0].text.wide);
    OBL_CHECK(f.m.pending[0].text.wideText == u"Pompa zepsuta ż\U0001F30A");
    OBL_CHECK(f.m.pending[0].text.key.empty());
    OBL_CHECK(!f.m.pending[1].text.wide);
    OBL_CHECK_EQ(f.m.pending[1].objectId, -2);
    OBL_CHECK(f.m.history.empty());
}

OBL_TEST_CASE("feedback.add ignores a silenced type, comparing only the low byte")
{
    Fixture f;
    AddScenarioMessage(f.m, "MESS_TYPE_BUILDING"); // type 4
    Add(f.m, f.env, "FEEDBACK_TOILET", 1, 4);
    AddWide(f.m, f.env, u"x", 1, 0x104); // low byte 4 as well
    OBL_CHECK(f.env.calls.empty());
    OBL_CHECK(f.m.history.empty());
    Add(f.m, f.env, "FEEDBACK_TOILET", 1, 5);
    OBL_CHECK_EQ(f.env.calls.size(), size_t(1));
}

OBL_TEST_CASE("feedback.pending queue drops the oldest beyond sixteen")
{
    Fixture f;
    f.env.canShow = false;
    for (int i = 0; i < 20; ++i) Add(f.m, f.env, ("K" + std::to_string(i)).c_str(), i, 5);
    OBL_REQUIRE(f.m.pending.size() == 16);
    OBL_CHECK_EQ(f.m.pending.front().text.key, std::string("K4"));
    OBL_CHECK_EQ(f.m.pending.back().text.key, std::string("K19"));
}

OBL_TEST_CASE("feedback.history keeps the last six")
{
    Fixture f;
    for (int i = 0; i < 9; ++i) {
        f.env.canShow = true; // the GUI frees the balloon again
        Add(f.m, f.env, ("K" + std::to_string(i)).c_str(), i, 5);
    }
    OBL_REQUIRE(f.m.history.size() == 6);
    OBL_CHECK_EQ(f.m.history.front().text.key, std::string("K3"));
    OBL_CHECK_EQ(f.m.history.back().text.key, std::string("K8"));
    AppendHistoryText(f.m, f.env, u"wide", 77);
    OBL_REQUIRE(f.m.history.size() == 6);
    OBL_CHECK_EQ(f.m.history.front().text.key, std::string("K4"));
    OBL_CHECK(f.m.history.back().text.wideText == u"wide");
}

OBL_TEST_CASE("feedback.history cursor walks oldest first and stops at the end")
{
    Fixture f;
    OBL_CHECK(BeginHistory(f.m) == nullptr);
    AppendHistoryKey(f.m, f.env, "A", 1);
    AppendHistoryKey(f.m, f.env, "B", 2);
    const HistoryEntry* entry = BeginHistory(f.m);
    OBL_REQUIRE(entry != nullptr);
    OBL_CHECK_EQ(entry->text.key, std::string("A"));
    entry = NextHistory(f.m);
    OBL_REQUIRE(entry != nullptr);
    OBL_CHECK_EQ(entry->text.key, std::string("B"));
    OBL_CHECK(NextHistory(f.m) == nullptr);
    OBL_CHECK(NextHistory(f.m) == nullptr); // past the end it stays there instead of stepping off
}

OBL_TEST_CASE("feedback.update shows the oldest waiting notice, short balloon above five")
{
    Fixture f;
    f.env.canShow = false;
    AddWide(f.m, f.env, u"first", 9, 5);
    for (int i = 0; i < 5; ++i) Add(f.m, f.env, "K", i, 5);
    f.env.canShow = true;
    Update(f.m, f.env);
    OBL_REQUIRE(f.env.calls.size() == 1);
    OBL_CHECK(!f.env.calls[0].key);
    OBL_CHECK(f.env.calls[0].text == u"first");
    OBL_CHECK_EQ(f.env.calls[0].mode, 1); // six were waiting
    OBL_CHECK_EQ(f.m.pending.size(), size_t(5));
    OBL_REQUIRE(f.m.history.size() == 1);
    OBL_CHECK_EQ(f.m.history[0].objectId, 9);
    OBL_CHECK(f.m.history[0].text.wide);

    f.env.canShow = true;
    Update(f.m, f.env);
    OBL_REQUIRE(f.env.calls.size() == 2);
    OBL_CHECK(f.env.calls[1].key);
    OBL_CHECK_EQ(f.env.calls[1].mode, 2); // five waiting: long balloon again
}

OBL_TEST_CASE("feedback.update shows an empty wide text as a wide text")
{
    Fixture f;
    f.env.canShow = false;
    AddWide(f.m, f.env, u"", 1, 5);
    f.env.canShow = true;
    Update(f.m, f.env);
    OBL_REQUIRE(f.env.calls.size() == 1);
    OBL_CHECK(!f.env.calls[0].key);
    OBL_CHECK(f.m.history[0].text.wide);
}

OBL_TEST_CASE("feedback.update leaves the queue alone while the balloon is busy")
{
    Fixture f;
    f.env.canShow = false;
    Add(f.m, f.env, "K", 1, 5);
    Update(f.m, f.env);
    OBL_CHECK(f.env.calls.empty());
    OBL_CHECK_EQ(f.m.pending.size(), size_t(1));
}

OBL_TEST_CASE("feedback.invalidate marks the object in both queues")
{
    Fixture f;
    Add(f.m, f.env, "K", 5, 5); // shown, history
    Add(f.m, f.env, "L", 5, 5); // pending (the balloon is busy now)
    Add(f.m, f.env, "M", 6, 5);
    InvalidateObject(f.m, 5);
    OBL_CHECK_EQ(f.m.history[0].objectId, -1);
    OBL_CHECK_EQ(f.m.pending[0].objectId, -1);
    OBL_CHECK_EQ(f.m.pending[1].objectId, 6);
}

OBL_TEST_CASE("feedback.remove drops waiting notices only, history stays")
{
    Fixture f;
    Add(f.m, f.env, "A", 1, 5); // shown
    Add(f.m, f.env, "A", 1, 5);
    Add(f.m, f.env, "B", 2, 5);
    AddWide(f.m, f.env, u"w", 1, 5);
    Add(f.m, f.env, "A", 3, 5);
    Add(f.m, f.env, "C", 1, 5);
    RemoveByObject(f.m, 1);
    OBL_REQUIRE(f.m.pending.size() == 2);
    OBL_CHECK_EQ(f.m.pending[0].text.key, std::string("B"));
    OBL_CHECK_EQ(f.m.pending[1].objectId, 3);
    RemoveByText(f.m, "A");
    OBL_REQUIRE(f.m.pending.size() == 1);
    OBL_CHECK_EQ(f.m.pending[0].text.key, std::string("B"));
    RemoveByText(f.m, "NOPE");
    RemoveByObject(f.m, 77);
    OBL_CHECK_EQ(f.m.pending.size(), size_t(1));
    OBL_CHECK_EQ(f.m.history.size(), size_t(1));
}

OBL_TEST_CASE("feedback.remove by object compares the id unsigned")
{
    Fixture f;
    f.env.canShow = false;
    Add(f.m, f.env, "A", -2, 5);
    RemoveByObject(f.m, static_cast<uint32_t>(-2));
    OBL_CHECK(f.m.pending.empty());
}

OBL_TEST_CASE("feedback.remove by text skips wide notices")
{
    Fixture f;
    f.env.canShow = false;
    AddWide(f.m, f.env, u"A", 1, 5);
    RemoveByText(f.m, "A");
    OBL_CHECK_EQ(f.m.pending.size(), size_t(1));
}

OBL_TEST_CASE("feedback.review waits half an hour and across midnight")
{
    Fixture f;
    f.env.counts[14] = 9; // toilets
    f.env.hour = 10.4f;
    Update(f.m, f.env);
    OBL_CHECK(f.env.calls.empty()); // 0.4 h since the last review
    OBL_CHECK_EQ(f.env.counts[14], uint32_t(9));

    f.env.hour = 10.6f;
    Update(f.m, f.env);
    OBL_REQUIRE(f.env.calls.size() == 1);
    OBL_CHECK_EQ(f.env.calls[0].keyText, std::string("FEEDBACK_TOILET"));
    OBL_CHECK_EQ(f.env.counts[14], uint32_t(0));
    OBL_CHECK_EQ(f.m.lastReviewHour, 10.6f);
    OBL_CHECK_EQ(f.env.draws, 1);

    // 23.9 -> 0.2: the later last-checked hour means the day wrapped; 0.3 h passed, so nothing happens
    Fixture g;
    g.env.counts[14] = 9;
    g.m.lastReviewHour = 23.9f;
    g.env.hour = 0.2f;
    Update(g.m, g.env);
    OBL_CHECK(g.env.calls.empty());
    g.env.hour = 0.5f;
    Update(g.m, g.env);
    OBL_CHECK_EQ(g.env.calls.size(), size_t(1));
}

OBL_TEST_CASE("feedback.review before the first one counts 25 as the previous hour")
{
    Fixture f;
    f.m.lastReviewHour = 25.0f;
    f.env.counts[3] = 6;
    f.env.hour = 1.0f; // 1 + 24 - 25 = 0: too soon
    Update(f.m, f.env);
    OBL_CHECK(f.env.calls.empty());
    f.env.hour = 2.0f; // 2 + 24 - 25 = 1 > 0.5
    Update(f.m, f.env);
    OBL_REQUIRE(f.env.calls.size() == 1);
    OBL_CHECK_EQ(f.env.calls[0].keyText, std::string("FEEDBACK_FIRSTAID"));
}

OBL_TEST_CASE("feedback.review picks one of the families above the threshold and resets all of them")
{
    Fixture f;
    f.env.hour = 11.0f;
    f.env.counts[1] = 6; // candidate 0
    f.env.counts[12] = 20; // sunbathing: never a candidate, but reset
    f.env.counts[13] = 20; // swimming fun likewise
    f.env.counts[5] = 7; // candidate 1
    f.env.counts[9] = 5; // not above the threshold: kept
    f.env.counts[20] = 100; // candidate 2
    f.env.randoms = {4}; // 4 % 3 = 1 -> family 5
    Update(f.m, f.env);
    OBL_REQUIRE(f.env.calls.size() == 1);
    OBL_CHECK_EQ(f.env.calls[0].keyText, std::string("FEEDBACK_MEALS"));
    OBL_CHECK_EQ(f.env.counts[1], uint32_t(0));
    OBL_CHECK_EQ(f.env.counts[5], uint32_t(0));
    OBL_CHECK_EQ(f.env.counts[12], uint32_t(0));
    OBL_CHECK_EQ(f.env.counts[13], uint32_t(0));
    OBL_CHECK_EQ(f.env.counts[20], uint32_t(0));
    OBL_CHECK_EQ(f.env.counts[9], uint32_t(5));
    OBL_CHECK_EQ(f.m.history[0].objectId, -2);
}

OBL_TEST_CASE("feedback.review with only excluded families resets nothing and draws nothing")
{
    Fixture f;
    f.env.hour = 11.0f;
    f.env.counts[12] = 20;
    Update(f.m, f.env);
    OBL_CHECK_EQ(f.env.draws, 0);
    OBL_CHECK_EQ(f.env.counts[12], uint32_t(20));
    OBL_CHECK_EQ(f.m.lastReviewHour, 11.0f);
}

OBL_TEST_CASE("feedback.complaint notice queues when Update just used the balloon")
{
    Fixture f;
    f.env.canShow = false;
    Add(f.m, f.env, "WAITING", 1, 5);
    f.env.canShow = true;
    f.env.hour = 11.0f;
    f.env.counts[14] = 6;
    Update(f.m, f.env);
    // the waiting notice took the balloon; the complaint found it busy and waits
    OBL_REQUIRE(f.env.calls.size() == 1);
    OBL_CHECK_EQ(f.env.calls[0].keyText, std::string("WAITING"));
    OBL_REQUIRE(f.m.pending.size() == 1);
    OBL_CHECK_EQ(f.m.pending[0].text.key, std::string("FEEDBACK_TOILET"));
    OBL_CHECK_EQ(f.m.history.size(), size_t(1));
}

OBL_TEST_CASE("feedback.scenario message words")
{
    Manager m;
    AddScenarioMessage(m, "MESS_TYPE_WORLD");
    AddScenarioMessage(m, "MESS_TYPE_OBJECT");
    OBL_CHECK_EQ(int(m.suppressWorld), 1);
    OBL_CHECK_EQ(int(m.suppressObject), 1);
    const char* words[] = {"MESS_TYPE_SCENERY", "MESS_TYPE_BUILDING", "MESS_TYPE_GUEST", "MESS_TYPE_VEHICLE", "MESS_TYPE_STAFF",
            "MESS_TYPE_LIFEGUARD", "MESS_TYPE_MECHANIC", "MESS_TYPE_REP", "MESS_TYPE_BUILDER", "MESS_TYPE_CLEANER",
            "MESS_TYPE_SECURITYGUARD"};
    for (const char* word : words) AddScenarioMessage(m, word);
    AddScenarioMessage(m, "mess_type_guest"); // compared case-sensitively
    AddScenarioMessage(m, "NOTHING");
    OBL_REQUIRE(m.suppressedCount == 11);
    for (uint32_t i = 0; i < 11; ++i) OBL_CHECK_EQ(m.suppressed[i], 3 + i);
}

OBL_TEST_CASE("feedback.scenario message list is bounded and keeps duplicates")
{
    Manager m;
    for (int i = 0; i < 150; ++i) AddScenarioMessage(m, "MESS_TYPE_GUEST");
    OBL_CHECK_EQ(m.suppressedCount, uint32_t(kMaxSuppressed));
    OBL_CHECK_EQ(m.suppressed[99], 5u);
    OBL_CHECK_EQ(m.lastReviewHour, 25.0f);
}

OBL_TEST_CASE("feedback.staff masks by object type")
{
    Manager m;
    const int types[] = {7, 11, 12, 8, 9, 10, 6};
    uint32_t bit = 1;
    for (int type : types) {
        MarkStaffMessage(m, bit, type);
        MarkStaffMessage(m, bit << 8, type);
        bit <<= 1;
    }
    OBL_CHECK_EQ(m.lifeGuardMessages, uint32_t(0x101));
    OBL_CHECK_EQ(m.cleanerMessages, uint32_t(0x202));
    OBL_CHECK_EQ(m.securityGuardMessages, uint32_t(0x404));
    OBL_CHECK_EQ(m.mechanicMessages, uint32_t(0x808));
    OBL_CHECK_EQ(m.repMessages, uint32_t(0x1010));
    OBL_CHECK_EQ(m.builderMessages, uint32_t(0x2020));
    OBL_CHECK_EQ(m.staffMessages, uint32_t(0x4040));
    OBL_CHECK_EQ(GetStaffMessages(m, 11), uint32_t(0x202));
    ClearStaffMessages(m, 11);
    OBL_CHECK_EQ(GetStaffMessages(m, 11), uint32_t(0));
    OBL_CHECK_EQ(GetStaffMessages(m, 7), uint32_t(0x101)); // the others stay
    // other types: nothing to read, mark or clear
    MarkStaffMessage(m, 0xff, 3);
    ClearStaffMessages(m, 13);
    OBL_CHECK_EQ(GetStaffMessages(m, 3), uint32_t(0));
    OBL_CHECK_EQ(m.staffMessages, uint32_t(0x4040));
}

OBL_TEST_CASE("feedback.commodity message keys")
{
    OBL_CHECK_EQ(std::string(CommodityMessageKey(0)), std::string("FEEDBACK_ACCOMMODATION"));
    OBL_CHECK_EQ(std::string(CommodityMessageKey(13)), std::string("FEEDBACK_SWIMMINGFUN"));
    OBL_CHECK_EQ(std::string(CommodityMessageKey(25)), std::string("FEEDBACK_SCARED"));
    OBL_CHECK(CommodityMessageKey(26) == nullptr);
    OBL_CHECK(CommodityMessageKey(0xffffffffu) == nullptr);
    for (uint32_t i = 0; i < 26; ++i) OBL_CHECK(CommodityMessageKey(i) != nullptr);
}
