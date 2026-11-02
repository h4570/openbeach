#include "game/objects/object_base.h"
#include "unit_test.h"

#include "game/objects/building_update.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace obl;

namespace {

// A building in a small world and an Env that records the calls in order. Objects are looked up by id like the object manager get object routine.
struct World {
    std::unique_ptr<tables::BuildingDef> def = std::make_unique<tables::BuildingDef>();
    std::unique_ptr<object::Building> building = std::make_unique<object::Building>();
    std::vector<std::string> calls;
    std::vector<messages::Message> posted;
    std::map<int32_t, object::Base*> objects;
    std::vector<std::unique_ptr<object::Base>> plain;
    std::vector<std::unique_ptr<object::Furniture>> tables;
    std::vector<std::unique_ptr<tables::TableDef>> tableDefs;
    std::vector<std::unique_ptr<object::TableChair[]>> chairs;
    bool started = false;
    int activeEvent = -1;
    int rands = 0;

    World()
    {
        def->m_FailPercent = 0; // the wear model is off unless a test turns it on
        building->m_definition = def.get();
        building->m_id = 7;
        std::strcpy(building->m_Name, "SOUVSHOP");
    }

    void Note(const char* call) { calls.push_back(call); }
    bool Called(const std::string& call) const
    {
        for (const std::string& c : calls)
            if (c == call) return true;
        return false;
    }
    size_t Index(const std::string& call) const
    {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i] == call) return i;
        return calls.size();
    }

    object::Base* AddObject(int32_t id, object::ObjectType type, int state = 0)
    {
        plain.push_back(std::make_unique<object::Base>());
        object::Base* object = plain.back().get();
        object->m_id = static_cast<uint32_t>(id);
        object->m_type = type;
        object->m_state = state;
        objects[id] = object;
        return object;
    }
    object::Furniture* AddTable(int32_t id, uint32_t seats, int32_t occupier)
    {
        tableDefs.push_back(std::make_unique<tables::TableDef>());
        tableDefs.back()->m_NumChairs = seats;
        chairs.push_back(std::make_unique<object::TableChair[]>(seats ? seats : 1));
        for (uint32_t i = 0; i < seats; ++i) chairs.back()[i].m_ChairOccupier = i + 1 == seats ? occupier : -1;
        tables.push_back(std::make_unique<object::Furniture>());
        object::Furniture* table = tables.back().get();
        table->m_definition = tableDefs.back().get();
        table->m_chairs = chairs.back().get();
        table->m_id = static_cast<uint32_t>(id);
        table->m_type = object::ObjectType::Table;
        objects[id] = table;
        return table;
    }

    // wear and tail
    int Rand()
    {
        Note("rand");
        return rands++;
    }
    void RequestInspection() { Note("inspect"); }
    void ClearQueueForBreakdown() { Note("clear queue"); }
    void HideStaff() { Note("hide"); }
    void NotifyBroken() { Note("notify"); }
    void RequestRepair() { Note("repair"); }
    void CloseDialog() { Note("dialog"); }
    void UpdateEvents() { Note("events"); }
    void UpdateOpenState() { Note("open state"); }
    float IsBuilding()
    {
        Note("is building");
        return 0.0f;
    }
    float IsDemolishing()
    {
        Note("is demolishing");
        return 0.0f;
    }
    bool IsCleanerClaimed()
    {
        Note("claimed");
        return false;
    }
    void SendCleaningMessage() { Note("message"); }
    void AddCleaningJob() { Note("job"); }
    void SetMoving() { Note("moving"); }
    void LoopSoundStart(const char* name) { Note((std::string("loop ") + name).c_str()); }
    void LoopSoundStop() { Note("loop stop"); }
    void BuildingSoundStart() { Note("sound start"); }
    void BuildingSoundStop() { Note("sound stop"); }
    // states 1 and 2
    bool IsStarted()
    {
        Note("is started");
        return started;
    }
    int ActiveEventId()
    {
        Note("event id");
        return activeEvent;
    }
    void CancelCurrentEvent() { Note("cancel event"); }
    void TickHoarding() { Note("hoarding"); }
    object::Base* GetObject(int32_t id)
    {
        auto it = objects.find(id);
        return it == objects.end() ? nullptr : it->second;
    }
    void Post(const messages::Message& message)
    {
        Note("post");
        posted.push_back(message);
    }
    void SetStarted() { Note("set started"); }

    UpdateReport Run(int32_t day = 3, float time = 12.0f, bool allowed = true)
    {
        return UpdateBuilding(*this, *building, day, time, allowed);
    }
};

} // namespace

// ---- the dispatcher ----------------------------------------------------------------------------------------------------------

OBL_TEST_CASE("building_update.working_states_run_head_wear_tail_and_the_sound_flag_in_order")
{
    for (int32_t state : {0, 4}) {
        World w;
        w.building->m_state = state;
        w.building->m_SelectLevel = 1;
        w.building->m_OpenStatus = 0;
        w.building->m_events = reinterpret_cast<object::BuildingEvents*>(1);
        w.def->m_FailPercent = 1000; // the wear model runs: AVFAILTIME 0 elapsed hours always roll, chance capped at 90
        w.def->m_AvFailTime = 0.0f;
        UpdateReport report = w.Run();
        OBL_CHECK_EQ(report.state, state);
        OBL_CHECK(report.wear.active);
        OBL_CHECK(report.tail.events);
        // dialog (head), rand (wear), events and open state (tail; a clean shop asks nothing more), then the sound flag of an open, silent building
        const std::vector<std::string> expected = {"dialog", "rand", "events", "open state", "sound start"};
        OBL_CHECK(w.calls == expected);
        OBL_CHECK_EQ(w.building->m_RepairState, 1u); // the roll (0) hit the 90 % chance
        OBL_CHECK_EQ(int{w.building->m_soundPlaying}, 1);
    }
}

OBL_TEST_CASE("building_update.other_states_only_run_the_sound_flag")
{
    for (int32_t state : {3, 5, 6, 100, -1, static_cast<int32_t>(0x80000000u)}) {
        World w;
        w.building->m_state = state;
        w.building->m_OpenStatus = 0;
        w.building->m_SelectLevel = 1;
        w.building->m_soundPlaying = 0;
        w.Run();
        const std::vector<std::string> expected = {"sound start"};
        OBL_CHECK(w.calls == expected);
        OBL_CHECK_EQ(int{w.building->m_soundPlaying}, 1);
    }
}

OBL_TEST_CASE("building_update.startup_ticks_the_hoarding")
{
    World w;
    w.building->m_state = 1;
    w.building->m_OpenStatus = 1;
    w.building->m_SelectLevel = 1;
    w.Run();
    const std::vector<std::string> expected = {"hoarding"};
    OBL_CHECK(w.calls == expected);
}

// ---- the demolition (state 2) -------------------------------------------------------------------------------------------------

namespace {

// A demolishing building with one vehicle, one table with seats, two loungers and staff of mixed classes, everybody gone.
void Furnish(World& w)
{
    object::Building& b = *w.building;
    b.m_state = 2;
    b.m_OpenStatus = 0;
    b.m_NumVehicles = 2;
    b.m_Vehicles[0] = 100;
    b.m_Vehicles[1] = 101;
    b.m_vehicleInUse[0] = 0;
    b.m_vehicleInUse[1] = 0;
    w.AddObject(100, object::ObjectType::Vehicle);
    w.AddObject(101, object::ObjectType::Vehicle);
    b.m_numTables = 2;
    b.m_Tables[0] = 200;
    b.m_Tables[1] = 201;
    w.AddTable(200, 4, -1);
    w.AddTable(201, 2, -1);
    b.m_numLoungers = 2;
    b.m_loungers[0] = 300;
    b.m_loungers[1] = 301;
    w.AddObject(300, object::ObjectType::Table, 0);
    w.AddObject(301, object::ObjectType::Table, 0);
    b.m_numMalestaff = 1;
    b.m_numFemalestaff = 1;
    b.m_staff[0] = 400;
    b.m_staff[1] = 401;
    w.AddObject(400, object::ObjectType::ServiceStaff);
    w.AddObject(401, object::ObjectType::Builder);
    b.m_NumNonServestaff = 2;
    b.m_NonServeStaff[0] = 500;
    b.m_NonServeStaff[1] = 501;
    w.AddObject(500, object::ObjectType::Cleaner);
    w.AddObject(501, object::ObjectType::ServiceStaff);
}

bool Marked(World& w, int32_t id)
{
    return w.objects.at(id)->m_waitingForDeath != 0;
}

} // namespace

OBL_TEST_CASE("building_update.demolition_in_progress_does_nothing_but_the_sound_flag")
{
    World w;
    Furnish(w);
    w.started = true;
    UpdateReport report = w.Run();
    OBL_CHECK(!report.demolish.waiting);
    OBL_CHECK(!Marked(w, 100));
    OBL_CHECK_EQ(w.building->m_OpenStatus, 0u);
    OBL_CHECK(!w.Called("hoarding"));
    OBL_CHECK(!w.Called("set started"));
}

OBL_TEST_CASE("building_update.waiting_demolition_closes_ticks_and_starts_when_empty")
{
    World w;
    Furnish(w);
    w.building->m_events = reinterpret_cast<object::BuildingEvents*>(1);
    w.activeEvent = 5;
    UpdateReport report = w.Run();
    OBL_CHECK(report.demolish.waiting);
    OBL_CHECK(report.demolish.eventCancelled);
    OBL_CHECK(report.demolish.emptied);
    OBL_CHECK_EQ(w.building->m_OpenStatus, 1u);
    OBL_CHECK(w.Index("is started") < w.Index("event id"));
    OBL_CHECK(w.Index("event id") < w.Index("cancel event"));
    OBL_CHECK(w.Index("cancel event") < w.Index("hoarding"));
    OBL_CHECK(w.Index("hoarding") < w.Index("post"));
    OBL_CHECK(w.Index("post") < w.Index("set started"));
    // everything the building owned is marked, the lists are forgotten (loungers all at once), the staff by class
    for (int32_t id : {100, 101, 200, 201, 300, 301, 400, 501}) OBL_CHECK(Marked(w, id));
    for (int32_t id : {401, 500}) OBL_CHECK(!Marked(w, id)); // a builder and a cleaner stay
    OBL_CHECK_EQ(w.building->m_NumVehicles, 0);
    OBL_CHECK_EQ(w.building->m_numTables, 0u);
    OBL_CHECK_EQ(w.building->m_numLoungers, 0u);
    OBL_CHECK_EQ(w.building->m_numMalestaff, 1); // the staff counts stay
    OBL_CHECK_EQ(w.posted.size(), size_t{2});
    for (size_t i = 0; i < 2; ++i) {
        OBL_CHECK_EQ(w.posted[i].sender, 7);
        OBL_CHECK_EQ(w.posted[i].receiver, 300 + static_cast<int>(i));
        OBL_CHECK_EQ(w.posted[i].type, int{messages::MT_STATECHANGE});
        OBL_CHECK_EQ(w.posted[i].data, 3);
    }
    OBL_CHECK_EQ(w.calls.back(), std::string("set started")); // closed and silent: the sound flag has nothing to do
}

OBL_TEST_CASE("building_update.demolition_does_not_cancel_an_idle_show_or_ask_without_a_schedule")
{
    World w;
    Furnish(w);
    w.building->m_events = reinterpret_cast<object::BuildingEvents*>(1);
    w.activeEvent = -1;
    w.Run();
    OBL_CHECK(w.Called("event id"));
    OBL_CHECK(!w.Called("cancel event"));

    World v;
    Furnish(v);
    v.activeEvent = 3;
    v.Run();
    OBL_CHECK(!v.Called("event id")); // no schedule: not even asked
}

OBL_TEST_CASE("building_update.demolition_waits_for_everything_to_clear")
{
    auto waits = [](auto&& setup) {
        World w;
        Furnish(w);
        setup(w);
        UpdateReport report = w.Run();
        OBL_CHECK(report.demolish.waiting);
        return !report.demolish.emptied && !w.Called("set started") && !Marked(w, 100) && w.building->m_NumVehicles == 2;
    };
    OBL_CHECK(waits([](World& w) { w.building->m_vehicleInUse[1] = 1; }));
    OBL_CHECK(waits([](World& w) { static_cast<object::Furniture*>(w.objects.at(201))->m_chairs[1].m_ChairOccupier = 55; }));
    OBL_CHECK(waits([](World& w) { w.objects.at(301)->m_state = 2; }));
    OBL_CHECK(waits([](World& w) { w.building->m_CurCapacity = 1; }));
    OBL_CHECK(waits([](World& w) { w.building->m_NumGuests = 3; }));
}

OBL_TEST_CASE("building_update.demolition_vehicle_flag_must_be_exactly_one_to_block")
{
    World w;
    Furnish(w);
    w.building->m_vehicleInUse[0] = 2;
    UpdateReport report = w.Run();
    OBL_CHECK(report.demolish.emptied);
}

OBL_TEST_CASE("building_update.demolition_capacity_test_is_signed_and_guests_unsigned")
{
    World w;
    Furnish(w);
    w.building->m_CurCapacity = -4; // < 1 (signed)
    OBL_CHECK(w.Run().demolish.emptied);

    World v;
    Furnish(v);
    v.building->m_NumGuests = -1; // nonzero
    OBL_CHECK(!v.Run().demolish.emptied);
}

OBL_TEST_CASE("building_update.demolition_skips_missing_objects_and_checks_the_staff_class")
{
    World w;
    Furnish(w);
    w.objects.erase(100);
    w.objects.erase(500);
    w.objects.erase(300);
    UpdateReport report = w.Run();
    OBL_CHECK(report.demolish.emptied);
    OBL_CHECK(Marked(w, 101));
    OBL_CHECK_EQ(w.posted.size(), size_t{2}); // the message goes to the id even when the object is gone
}

OBL_TEST_CASE("building_update.chairs_are_empty_when_no_seat_has_an_occupier")
{
    World w;
    object::Furniture* free = w.AddTable(1, 4, -1);
    OBL_CHECK(AreChairsEmpty(*free));
    object::Furniture* taken = w.AddTable(2, 4, 9);
    OBL_CHECK(!AreChairsEmpty(*taken));
    object::Furniture* none = w.AddTable(3, 0, -1);
    OBL_CHECK(AreChairsEmpty(*none));
    free->m_chairs[0].m_ChairOccupier = 0; // guest id 0 is a guest
    OBL_CHECK(!AreChairsEmpty(*free));
}
