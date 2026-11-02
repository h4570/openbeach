#include "byte_copy.h"
#include "unit_test.h"

#include "importer/src/readers/building_behavior_keys.h"
#include "game/objects/building_update_tail.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;

namespace {

// A building and an Env that records every call in order, with what the building showed at the call, and answers the construction queries and
// the cleaner claim from its fields.
struct Tail {
    tables::BuildingDef definition{};
    std::unique_ptr<object::Building> building = std::make_unique<object::Building>();
    std::vector<std::string> calls;
    std::vector<std::unique_ptr<object::Building>> seen;
    float building_ = 0.0f;
    float demolishing_ = 0.0f;
    bool claimed_ = false;

    Tail()
    {
        building->m_definition = &definition;
        SetName("SOUVSHOP");
    }
    void SetName(const char* name)
    {
        definition.m_Behavior = BehaviorFromKey(name);
        std::memset(building->m_Name, 0, sizeof building->m_Name);
        std::strncpy(building->m_Name, name, sizeof building->m_Name - 1);
    }
    void Note(const std::string& call)
    {
        calls.push_back(call);
        obl::test::ByteCopy(*seen.emplace_back(std::make_unique<object::Building>()), *building);
    }
    void CloseDialog() { Note("dialog"); }
    void UpdateEvents() { Note("events"); }
    void UpdateOpenState() { Note("open state"); }
    float IsBuilding()
    {
        Note("is building");
        return building_;
    }
    float IsDemolishing()
    {
        Note("is demolishing");
        return demolishing_;
    }
    bool IsCleanerClaimed()
    {
        Note("claimed");
        return claimed_;
    }
    void SendCleaningMessage() { Note("message"); }
    void AddCleaningJob() { Note("job"); }
    void SetMoving() { Note("moving"); }
    void LoopSoundStart(const char* name) { Note(std::string("loop ") + name); }
    void LoopSoundStop() { Note("loop stop"); }
    void BuildingSoundStart() { Note("sound start"); }
    void BuildingSoundStop() { Note("sound stop"); }
    TailReport Run() { return UpdateWorkingTail(*this, *building); }
    bool Called(const std::string& call) const
    {
        for (const std::string& c : calls)
            if (c == call) return true;
        return false;
    }
};

} // namespace

// ---- the head ----------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_update_tail.head_recomputes_the_profit_and_closes_the_dialog_of_a_selected_building")
{
    Tail t;
    t.building->m_incomeToday = 120.5f;
    t.building->m_costToday = 20.25f;
    t.building->m_SelectLevel = 0;
    BeginWorkingUpdate(t, *t.building);
    OBL_CHECK_EQ(t.building->m_profitToday, 100.25f);
    OBL_CHECK(t.calls.empty());

    t.building->m_SelectLevel = 1;
    t.building->m_costToday = 200.0f;
    BeginWorkingUpdate(t, *t.building);
    OBL_CHECK_EQ(t.building->m_profitToday, -79.5f);
    OBL_CHECK_EQ(t.calls.size(), size_t{1});
    OBL_CHECK(t.Called("dialog"));
}

OBL_TEST_CASE("building_update_tail.head_closes_the_dialog_only_for_select_level_one")
{
    for (int level : {-1, 0, 2, 3}) {
        Tail t;
        t.building->m_SelectLevel = level;
        BeginWorkingUpdate(t, *t.building);
        OBL_CHECK(t.calls.empty());
    }
}

// ---- events and open state ---------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_update_tail.events_run_only_with_a_schedule_and_before_the_open_state")
{
    Tail t;
    TailReport report = t.Run();
    OBL_CHECK(!report.events);
    OBL_CHECK_EQ(t.calls.size(), size_t{1}); // an ordinary shop with no sound: just the open state
    OBL_CHECK_EQ(t.calls[0], std::string("open state"));

    Tail s;
    s.building->m_events = reinterpret_cast<object::BuildingEvents*>(1);
    report = s.Run();
    OBL_CHECK(report.events);
    OBL_CHECK_EQ(s.calls[0], std::string("events"));
    OBL_CHECK_EQ(s.calls[1], std::string("open state"));
}

// ---- the toilet ----------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_update_tail.toilet_asks_for_a_cleaner_after_more_than_25_visits")
{
    Tail t;
    t.SetName("LOO");
    t.building->m_VisitsSinceCleaned = 25;
    TailReport report = t.Run();
    OBL_CHECK(!report.cleaning);
    OBL_CHECK(!t.Called("message"));

    Tail u;
    u.SetName("LOO");
    u.building->m_VisitsSinceCleaned = 26;
    report = u.Run();
    OBL_CHECK(report.cleaning);
    const std::vector<std::string> expected = {
            "open state", "is building", "is demolishing", "claimed", "message", "job", "moving", "loop flies"};
    OBL_CHECK(u.calls == expected);
}

OBL_TEST_CASE("building_update_tail.toilet_cleaning_needs_every_condition")
{
    auto run = [](auto&& setup) {
        Tail t;
        t.SetName("LOO");
        t.building->m_VisitsSinceCleaned = 40;
        setup(t);
        t.Run();
        return t.Called("job");
    };
    OBL_CHECK(run([](Tail&) {}));
    OBL_CHECK(!run([](Tail& t) { t.building->m_calledCleaning = 1; }));
    OBL_CHECK(!run([](Tail& t) { t.building_ = 1.0f; }));
    OBL_CHECK(!run([](Tail& t) { t.building_ = -0.5f; }));
    OBL_CHECK(!run([](Tail& t) { t.demolishing_ = 0.25f; }));
    OBL_CHECK(!run([](Tail& t) { t.claimed_ = true; }));
    OBL_CHECK(run([](Tail& t) { t.building_ = std::numeric_limits<float>::quiet_NaN(); }));
    OBL_CHECK(run([](Tail& t) { t.demolishing_ = std::numeric_limits<float>::quiet_NaN(); }));
    OBL_CHECK(run([](Tail& t) { t.building_ = -0.0f; }));
}

OBL_TEST_CASE("building_update_tail.toilet_checks_stop_at_the_first_failing_one")
{
    Tail t;
    t.SetName("LOO");
    t.building->m_VisitsSinceCleaned = 40;
    t.building_ = 1.0f;
    t.Run();
    OBL_CHECK(t.Called("is building"));
    OBL_CHECK(!t.Called("is demolishing"));
    OBL_CHECK(!t.Called("claimed"));

    Tail u; // the name is the last test: a shop with a dirty counter does not even ask the construction manager when called-cleaning is set
    u.building->m_VisitsSinceCleaned = 40;
    u.building->m_calledCleaning = 1;
    u.Run();
    OBL_CHECK(!u.Called("is building"));
}

OBL_TEST_CASE("building_update_tail.other_buildings_with_many_visits_are_not_cleaned")
{
    Tail t;
    t.building->m_VisitsSinceCleaned = 1000;
    TailReport report = t.Run();
    OBL_CHECK(!report.cleaning);
    OBL_CHECK(!t.Called("job"));
    OBL_CHECK(t.Called("is building")); // asked, then rejected by the name
}

OBL_TEST_CASE("building_update_tail.negative_visits_count_as_a_huge_unsigned_number")
{
    Tail t;
    t.SetName("LOO");
    t.building->m_VisitsSinceCleaned = -1;
    TailReport report = t.Run();
    OBL_CHECK(report.cleaning);
    OBL_CHECK(report.flies == LoopCall::Start);
}

OBL_TEST_CASE("building_update_tail.names_are_compared_whole")
{
    Tail t;
    t.SetName("LOOS");
    t.building->m_VisitsSinceCleaned = 40;
    t.Run();
    OBL_CHECK(!t.Called("job"));
    OBL_CHECK(!t.Called("loop flies"));
    t.SetName("LO");
    t.Run();
    OBL_CHECK(!t.Called("loop flies"));
}

OBL_TEST_CASE("building_update_tail.flies_start_above_20_visits_and_the_loop_stops_otherwise")
{
    Tail t;
    t.SetName("LOO");
    t.building->m_calledCleaning = 1; // out of the cleaning branch
    t.building->m_VisitsSinceCleaned = 20;
    TailReport report = t.Run();
    OBL_CHECK(report.flies == LoopCall::Stop);
    OBL_CHECK_EQ(t.calls.back(), std::string("loop stop"));

    Tail u;
    u.SetName("LOO");
    u.building->m_calledCleaning = 1;
    u.building->m_VisitsSinceCleaned = 21;
    report = u.Run();
    OBL_CHECK(report.flies == LoopCall::Start);
    OBL_CHECK_EQ(u.calls.back(), std::string("loop flies"));
}

// ---- the sound of the other buildings ------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_update_tail.a_building_with_its_sound_playing_asks_for_it_again_every_update")
{
    Tail t;
    t.building->m_soundPlaying = 1;
    TailReport report = t.Run();
    OBL_CHECK(report.buildingSound);
    OBL_CHECK(t.Called("sound start"));

    Tail u;
    u.building->m_soundPlaying = 0;
    report = u.Run();
    OBL_CHECK(!report.buildingSound);
    OBL_CHECK(!u.Called("sound start"));

    Tail loo; // a toilet has the flies instead
    loo.SetName("LOO");
    loo.building->m_soundPlaying = 1;
    loo.building->m_calledCleaning = 1;
    loo.Run();
    OBL_CHECK(!loo.Called("sound start"));
}

// ---- the hire buildings --------------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_update_tail.hire_buildings_idle_while_open_with_a_free_vehicle")
{
    for (const char* name : {"NJJETSKIHIRE", "NJSPDBOATHIRE"}) {
        Tail t;
        t.SetName(name);
        t.building->m_NumVehicles = 3;
        t.building->m_vehicleInUse[0] = 1;
        t.building->m_vehicleInUse[1] = 1;
        t.building->m_vehicleInUse[2] = 0;
        t.building->m_OpenStatus = 0;
        TailReport report = t.Run();
        OBL_CHECK(report.hire == LoopCall::Start);
        OBL_CHECK_EQ(t.calls.back(), std::string("loop newjetskiidle"));
    }
}

OBL_TEST_CASE("building_update_tail.hire_buildings_stop_when_closed_busy_or_empty")
{
    Tail closed;
    closed.SetName("NJJETSKIHIRE");
    closed.building->m_NumVehicles = 2;
    closed.building->m_OpenStatus = 1;
    OBL_CHECK(closed.Run().hire == LoopCall::Stop);
    OBL_CHECK_EQ(closed.calls.back(), std::string("loop stop"));

    Tail busy;
    busy.SetName("NJJETSKIHIRE");
    busy.building->m_NumVehicles = 2;
    busy.building->m_vehicleInUse[0] = 1;
    busy.building->m_vehicleInUse[1] = 1;
    OBL_CHECK(busy.Run().hire == LoopCall::Stop);

    Tail none;
    none.SetName("NJSPDBOATHIRE");
    none.building->m_NumVehicles = 0;
    OBL_CHECK(none.Run().hire == LoopCall::Stop);

    // a free flag beyond the counted vehicles does not count
    Tail beyond;
    beyond.SetName("NJJETSKIHIRE");
    beyond.building->m_NumVehicles = 1;
    beyond.building->m_vehicleInUse[0] = 1;
    beyond.building->m_vehicleInUse[1] = 0;
    OBL_CHECK(beyond.Run().hire == LoopCall::Stop);
}

OBL_TEST_CASE("building_update_tail.hire_loop_ignores_other_buildings")
{
    Tail t;
    t.building->m_NumVehicles = 2;
    OBL_CHECK(t.Run().hire == LoopCall::None);
    OBL_CHECK(!t.Called("loop stop"));
    OBL_CHECK(!t.Called("loop newjetskiidle"));
}

// ---- the sound flag at the end ---------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_update_tail.sound_flag_follows_the_opening")
{
    Tail t;
    t.building->m_soundPlaying = 0;
    t.building->m_OpenStatus = 0;
    UpdateSoundFlag(t, *t.building);
    OBL_CHECK_EQ(int{t.building->m_soundPlaying}, 1);
    OBL_CHECK(t.Called("sound start"));
    OBL_CHECK_EQ(int{t.seen[0]->m_soundPlaying}, 0); // the flag is set after the call

    t.calls.clear();
    UpdateSoundFlag(t, *t.building); // open and playing: nothing
    OBL_CHECK(t.calls.empty());

    t.building->m_OpenStatus = 1;
    UpdateSoundFlag(t, *t.building);
    OBL_CHECK_EQ(int{t.building->m_soundPlaying}, 0);
    OBL_CHECK(t.Called("sound stop"));
    OBL_CHECK_EQ(int{t.seen.back()->m_soundPlaying}, 1); // cleared after the call

    t.calls.clear();
    UpdateSoundFlag(t, *t.building); // closed and silent: nothing
    OBL_CHECK(t.calls.empty());
}

OBL_TEST_CASE("building_update_tail.sound_flag_ignores_other_open_statuses")
{
    for (uint32_t status : {2u, 7u, 0xffffffffu}) {
        Tail playing;
        playing.building->m_soundPlaying = 1;
        playing.building->m_OpenStatus = status;
        UpdateSoundFlag(playing, *playing.building);
        OBL_CHECK(playing.calls.empty());
        OBL_CHECK_EQ(int{playing.building->m_soundPlaying}, 1);

        Tail silent;
        silent.building->m_soundPlaying = 0;
        silent.building->m_OpenStatus = status;
        UpdateSoundFlag(silent, *silent.building);
        OBL_CHECK(silent.calls.empty());
        OBL_CHECK_EQ(int{silent.building->m_soundPlaying}, 0);
    }
}
