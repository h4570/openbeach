#include "game/events/disaster_placement.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/weather/weather_state.h"
#include "game/events/event_manager.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/events/event_lifecycle.h"
#include "game/world/save_table_reader.h"
#include "game/game/game_accessors.h"
#include "game/game/diagnostics.h"
#include "game/game/rng.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "core/log.h"
#include "formats/map_pack.h"
#include "game/map/grid_lifecycle.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/weather/weather.h"
#include "game/world/world_receive.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace obl::disaster {
namespace {

using disaster::Event;
using disaster::Manager;
using messages::Message;
using object::Building;
using MessageHandler = messages::Handler;
using weather::Weather;

struct GameEnv {
    Manager& m;
    bool bindGlobal = true;
    std::vector<Building*> buildings;
    std::vector<uint32_t> sharks;
    bool readBuildings = false, readSharks = false;
    explicit GameEnv(Manager& state, bool bind = true) : m(state), bindGlobal(bind) {}
    Manager& State() { return m; }
    uint32_t Day() { return game::GetWorld()->m_gameDay; }
    float Hour() { return game::GetWorld()->m_gameDayTime; }
    int Rand() { return game::Rand(); }
    void InvalidInsertion() { game::RaiseAccessViolation(); }
    disaster::Event& Event(size_t i) { return m.schedule[i]; }
    int Active(size_t i) { return m.active[i]; }
    void AddActive(int index) { m.active.push_back(index); }
    size_t ActiveSize() const { return m.active.size(); }
    void SetGlobal(bool value)
    {
        if (bindGlobal) event_facade::SetCurrent(value ? &m : nullptr);
    }
    void RemoveActive(size_t i)
    {
        m.active[i] = m.active[m.numCurEvents - 1];
        m.active.pop_back();
    }
    void Insert(size_t i, const disaster::Event& input) { m.schedule.insert(m.schedule.begin() + static_cast<std::ptrdiff_t>(i), input); }
    size_t BuildingCount()
    {
        if (!readBuildings) {
            const auto list = object::object_manager::GetListByClass(game::GetObjectManager(), 2);
            const auto count = static_cast<uint32_t>(list.size());
            for (uint32_t i = 0; i < count; ++i) buildings.push_back(static_cast<Building*>(list[i]));
            readBuildings = true;
        }
        return buildings.size();
    }
    int BuildingBeer(size_t i) { return buildings[i]->m_BeerType; }
    void Beer(size_t i, int beer) { object::building_api::SetBeerType(buildings[i], beer); }
    void Strength(size_t i, float strength) { object::building_api::SetBeerStrength(buildings[i], strength); }
    size_t SharkCount()
    {
        if (!readSharks) {
            const auto list = object::object_manager::GetListByClass(game::GetObjectManager(), 13);
            const auto count = static_cast<uint32_t>(list.size());
            for (uint32_t i = 0; i < count; ++i) sharks.push_back(list[i]->m_id);
            readSharks = true;
        }
        return sharks.size();
    }
    void Finished(size_t i, int index)
    {
        Message message{};
        message.sender = index;
        message.receiver = sharks[i];
        message.type = messages::MT_DISASTER_FINISHED;
        message.data = index;
        messages::message_facade::Post(game::GetWorld()->m_messageHandler, &message);
    }
    void Shark(int index, const int* rect, float x, float y)
    {
        Message message{};
        message.sender = message.receiver = -2;
        message.data = 13;
        disaster::DisasterPlacement placement{};
        std::strcpy(placement.m_Name, "Shark");
        placement.m_EventIndex = index;
        std::memcpy(placement.m_Rect, rect, sizeof(placement.m_Rect));
        placement.m_Position[0] = x;
        placement.m_Position[1] = y;
        world::PostPlaceCommand(message, &placement);
    }
    void Water(float value) { game::GetWorld()->m_waterContam = value; }
    void Available(int beer, bool value) { game::GetWorldParams()->m_beers[beer].m_scriptFlag = value; }
    void BeerMessage(int beer, bool restored)
    {
        std::string key = restored ? "BEER_RESTORED_TYPE_" : "BEER_SHORTAGE_TYPE_";
        key += game::GetWorldParams()->m_beers[beer].m_key;
        game::GetGuiManager()->ShowMessageBox("MessageOKBox", key.c_str(), nullptr, true);
    }
    void Thunder(int level) { weather::runtime::SetThunderLevel(game::GetWeather(), level); }
    void Area(const int* rect) { weather::runtime::SetLightningArea(game::GetWeather(), rect[0], rect[1], rect[2], rect[3]); }
    void Target(const char* name) { weather::runtime::SetLightningTargetByName(game::GetWeather(), name); }
};

test::CallCounter g_addCalls("events.manager-add-event"), g_updateCalls("events.manager-update");
test::CallCounter g_constructCalls("events.manager-construct"), g_destructCalls("events.manager-destruct"),
        g_loadCalls("events.manager-load");
// Drives the save/load lifecycle fixture; independent of the LoadJson counter.
test::CallCounter g_lifecycleFixture("events.manager-lifecycle-fixture");

} // namespace

namespace event_facade {

Manager* Create()
{
    auto* manager = new Manager();
    GameEnv env(*manager);
    disaster::Construct(env);
    g_constructCalls.Count();
    return manager;
}

void Destroy(Manager* manager)
{
    GameEnv env(*manager);
    disaster::Destruct(env);
    g_destructCalls.Count();
    delete manager;
}

bool LoadJson(Manager* manager, const nlohmann::json& in, std::string& error)
{
    GameEnv env(*manager);
    world_save::PlainTableReader reader("EventManager", nullptr, in, error);
    if (reader.Ok()) disaster::LoadFields(env, reader);
    g_loadCalls.Count();
    return reader.Ok();
}

Manager* Current()
{
    return game::GetContext().eventManager;
}

void SetCurrent(Manager* value)
{
    game::GetContext().eventManager = value;
}

void AddEvent(Manager* manager, Event event)
{
    GameEnv env(*manager);
    disaster::AddEvent(env, event);
    g_addCalls.Count();
}

void Update(Manager* manager)
{
    GameEnv env(*manager);
    disaster::Update(env);
    g_updateCalls.Count();
}

} // namespace event_facade

namespace {

// Exercise real side effects on a fresh scripted scenario: crossing deque blocks, equal-time insertion, strict boundaries,
// swap removal, duration crossing midnight, every disaster kind and the beer replacement in an existing bar.
void Drive(int rounds)
{
    auto* world = game::GetWorld();
    if (!world || !world->m_eventManager) {
        OBL_LOG("autotest: FAILED event driver needs gameplay");
        return;
    }
    auto& m = *world->m_eventManager;
    GameEnv fixture(m);
    // Save serializes counts and indices, but rebuilds records from WORLD_SCRIPT. Synthetic
    // records must be scoped to this driver or the next Load would index past that schedule.
    const Manager before = m;
    const int savedDay = world->m_gameDay;
    const float savedHour = world->m_gameDayTime;
    int beer = -1;
    for (size_t i = 0; i < fixture.BuildingCount(); ++i) {
        if (fixture.BuildingBeer(i) >= 0) {
            beer = fixture.BuildingBeer(i);
            break;
        }
    }
    if (beer < 0 || game::GetWorldParams()->m_numBeers < 2) {
        OBL_LOG("autotest: FAILED event driver needs a bar");
        return;
    }
    std::array<int, 4> eventArea;
    std::string areaError;
    if (!map_pack::ReadEventArea(map::PackDirectory(), game::GetWorld()->m_grid->m_gridFileName, eventArea, areaError)) {
        game::FatalError("%s", areaError.c_str());
        return;
    }
    for (int round = 0; round < rounds; ++round) {
        int day = std::max(world->m_gameDay, static_cast<int>(fixture.Event(m.numEvents - 1).day + 1));
        world->m_gameDay = day;
        world->m_gameDayTime = 9;
        for (int i = 0; i < 40; ++i) {
            Event event{};
            event.type = 1;
            event.day = day;
            event.time = 9.125f;
            event.duration = 1.5f;
            event.disasterValue = i * 0.01f;
            if (i == 39) event.duration = std::numeric_limits<float>::quiet_NaN();
            event_facade::AddEvent(&m, event);
        }
        for (int type : {0, 2, 3, 4}) {
            Event event{};
            event.type = type;
            event.day = day;
            event.time = 9.125f;
            event.duration = 1.5f;
            event.scale = type == 0 ? 3 : 2;
            std::memcpy(event.rect, eventArea.data(), sizeof(event.rect));
            event.beerType = beer;
            event.replacementBeer = (beer + 1) % game::GetWorldParams()->m_numBeers;
            event.replacementStrength = 1;
            event.target = "POWERGEN";
            event_facade::AddEvent(&m, event);
        }
        Event overnight{};
        overnight.type = 1;
        overnight.day = day;
        overnight.time = 9.5f;
        overnight.duration = 25;
        overnight.disasterValue = 0.5f;
        event_facade::AddEvent(&m, overnight);
        for (float hour : {9.125f, 9.126f, 9.5f, 9.501f, 10.625f, 10.626f}) {
            world->m_gameDayTime = hour;
            event_facade::Update(&m);
        }
        ++world->m_gameDay;
        for (float hour : {10.5f, 10.501f}) {
            world->m_gameDayTime = hour;
            event_facade::Update(&m);
        }
    }
    // The synthetic events and the counts go back to the scripted state; the comparison checks that every field came back.
    m.schedule.resize(before.schedule.size());
    for (size_t i = 0; i < before.schedule.size(); ++i) m.schedule[i] = before.schedule[i];
    m.active = before.active;
    m.nextEvent = before.nextEvent;
    m.numEvents = before.numEvents;
    m.numCurEvents = before.numCurEvents;
    m.dayOffset = before.dayOffset;
    m.curDayOffset = before.curDayOffset;
    world->m_gameDay = savedDay;
    world->m_gameDayTime = savedHour;
    const bool restoredAll = m == before;
    static test::CallCounter restored("events.manager-burst-restored");
    restored.Record(restoredAll, restoredAll ? std::string() : std::string("event burst restore: state differs from the scripted one"));
    if (!restoredAll) OBL_LOG("autotest: FAILED event burst restore: state differs from the scripted one");
}

void DriveLifecycle(int)
{
    auto* world = game::GetWorld();
    if (!world || !world->m_eventManager || !world->m_eventManager->numEvents) {
        OBL_LOG("autotest: FAILED event lifecycle driver needs scripted events");
        return;
    }
    GameEnv env(*world->m_eventManager);
    auto& m = env.m;
    m.active.clear();
    m.numCurEvents = 0;
    for (int i = 0; i < 40; ++i) {
        const int id = i % m.numEvents;
        env.AddActive(id);
        ++m.numCurEvents;
        const auto& event = env.Event(id);
        if (event.type == 2) env.Available(event.beerType, false);
    }
    m.nextEvent = m.numEvents;
    m.dayOffset = 2;
    m.curDayOffset = 3;
    for (int i = 0; i < m.numEvents; ++i) env.Event(i).day += 3;
    g_lifecycleFixture.Count();
    OBL_LOG("autotest: event lifecycle fixture active=%zu events=%d offset=%d", env.ActiveSize(), m.numEvents, m.curDayOffset);
}

} // namespace

bool RegisterEventTestDrivers()
{
    g_updateCalls.SetDriver(&Drive);
    g_lifecycleFixture.SetDriver(&DriveLifecycle);
    return true;
}
} // namespace obl::disaster
