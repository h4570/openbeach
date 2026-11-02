#include "game/ui/gui_action_entries.h"
#include "core/ascii_case.h"
#include "building_events.h"
#include "game/objects/building_methods.h"
#include "game/construction/construction_state.h"
#include "game/objects/object_registry_api.h"
#include "flow.h"
#include "world_state.h"

#include "importer/src/readers/save/save_writer.h"
#include "formats/schemas.h"
#include "core/json_schema.h"
#include "core/pack_path.h"
#include "game/objects/building_events_trace.h"
#include "game/game/save_load.h"
#include "game/ui/save_slots.h"
#include "game/ui/save_slot_header.h"
#include "game/objects/building_events_state.h"
#include "game/objects/object_building.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "core/call_counter.h"
#include "core/log.h"
#include "test_util.h"
#include "core/os.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include "game/ui/map_view_access.h"
#include "game/ui/map_view.h"

namespace obl::test {
namespace {
std::string g_updateBefore;
int g_savedSlot = -1;
std::map<int, std::pair<std::string, int>> g_savedEvents;
int g_expectedLoadIndex = -1;

std::vector<int> Ids(const std::deque<int32_t>& deque)
{
    return {deque.begin(), deque.end()};
}

std::string State(const object::BuildingEvents* events, bool includeIndex = true)
{
    std::ostringstream out;
    out << "owner=" << events->m_OwnerID;
    if (includeIndex) out << " slot=" << events->m_CurrentSlot;
    out << " active=" << int(events->m_eventActive);
    for (int i = 0; i < 3; ++i) {
        const object::EventSlot& slot = events->m_slots[i];
        out << " [" << i << " event=" << slot.m_EventID << " start=" << slot.m_start << " duration=" << slot.m_duration
            << " price=" << slot.m_price;
        // The deque length, not NumActors/NumGuests: the constructor leaves NumActors uninitialized
        // until the first show, so heap leftovers would make the dump differ between runs.
        auto actors = Ids(slot.m_actors);
        out << " actors=" << actors.size() << ":";
        for (int id : actors) out << id << ",";
        auto guests = Ids(slot.m_guests);
        out << " guests=" << guests.size() << ":";
        for (int id : guests) out << id << ",";
        out << "]";
    }
    return out.str();
}

void Trace(const char* operation, const object::BuildingEvents* events)
{
    OBL_LOG("autotest: events %s %s", operation, State(events).c_str());
}

void ObserveCall(object::BuildingEventsCall call, object::BuildingEvents* events, bool after)
{
    switch (call) {
    case object::BuildingEventsCall::Update:
        if (!after)
            g_updateBefore = State(events);
        else if (State(events) != g_updateBefore)
            Trace("update", events);
        break;
    case object::BuildingEventsCall::AdvanceSlot: Trace(after ? "advance-after" : "advance-before", events); break;
    }
}

// Runs after the production function (game/objects/building_events.cpp) has repaired the slot index.
void Load(object::BuildingEvents* events)
{
    Trace("load-after", events);
    if (const auto saved = g_savedEvents.find(events->m_OwnerID); saved != g_savedEvents.end()) {
        const auto& [state, savedIndex] = saved->second;
        int expectedIndex = g_expectedLoadIndex < 0 ? savedIndex : g_expectedLoadIndex;
        OBL_LOG("autotest: %s event save/load state comparison",
                State(events, false) == state && events->m_CurrentSlot == expectedIndex ? "PASS" : "FAILED");
        OBL_LOG("autotest: event index saved=%d loaded=%d expected=%d", savedIndex, events->m_CurrentSlot, expectedIndex);
    }
}

object::Building* Stage()
{
    object::Manager* manager = game::GetObjectManager();
    if (!manager) return nullptr;
    for (uint32_t i = 0; i < manager->m_buildings.size(); ++i) {
        auto* building = static_cast<object::Building*>(manager->m_buildings[i]);
        if (building && ascii::EqualsNoCase(building->m_Name, "STAGE")) return building;
    }
    return nullptr;
}

bool Matches(const object::BuildingEvents* events, const std::vector<std::string>& args)
{
    int slot = events->m_CurrentSlot;
    if (slot < 0 || slot >= 3 || args.size() < 4) return false;
    const object::EventSlot& data = events->m_slots[slot];
    return slot == std::stoi(args[0]) && int(events->m_eventActive) == std::stoi(args[1]) &&
            data.m_actors.size() == static_cast<size_t>(std::stoi(args[2])) &&
            data.m_guests.size() == static_cast<size_t>(std::stoi(args[3]));
}

void Check(bool pass, const char* label)
{
    OBL_LOG("autotest: %s events contract %s", pass ? "PASS" : "FAILED", label);
}

} // namespace

bool AwaitBuildingEventState(const std::vector<std::string>& args)
{
    static uint32_t started = 0;
    if (!started) started = os::TickMs();
    auto stage = Stage();
    object::BuildingEvents* events = stage ? stage->m_events : nullptr;
    bool matches = events && Matches(events, args);
    if (!matches && args.size() == 5 && os::TickMs() - started < std::stoul(args[4])) return false;
    OBL_LOG("autotest: %s event-await", matches ? "PASS" : "FAILED");
    if (events) Trace("await", events);
    started = 0;
    return true;
}

bool LoadSaveSlot(const std::string& slotArg, bool compareSavedEvents)
{
    if (!compareSavedEvents && slotArg != "last") ForgetBuildingEvents();
    game::Game* game = Game();
    int slot = slotArg == "last" ? g_savedSlot : std::stoi(slotArg);
    if (!game || slot < 0) {
        OBL_LOG("autotest: FAILED no saved slot");
        return false;
    }
    OBL_LOG("autotest: load-slot %d", slot);
    ui::save_panel::ScanSaveSlots();
    const auto& slots = game->m_saveSlots;
    const auto entry = std::find_if(slots.begin(), slots.end(), [&](const auto& record) { return record->m_number == slot; });
    if (entry == slots.end()) {
        OBL_LOG("autotest: FAILED save slot %d not found", slot);
        return false;
    }
    // The pending value is an index in the scanned vector, not the file slot number. game_api::Update loads it when
    // the main menu is the current state, so switching to it works from any state (including the intro).
    game->m_loadSlot = static_cast<int>(entry - slots.begin());
    game->m_nextState = game->m_states[static_cast<int>(game::GameStateId::MainMenu)].get();
    NotifyWorldReloading();
    return true;
}

// `obl-convert save` in process: an old save file as a validated obl.save at `output`, with the keys and identity of the
// game's selected pack.
bool ConvertSave(const std::string& save, const std::string& output)
{
    json_schema::Registry schemas;
    std::string error;
    const bool ok = formats::LoadEmbeddedSchemas(schemas, error) &&
            convert::ConvertOldSave(os::ResolvePath(save), pack_path::Selected(), os::ResolvePath(output), schemas, error);
    if (ok)
        OBL_LOG("autotest: convert-save %s -> %s", save.c_str(), output.c_str());
    else
        OBL_LOG("autotest: FAILED convert-save %s: %s", save.c_str(), error.c_str());
    return ok;
}

// Loads an obl.save through the bridge at the next main-menu frame, the same frame timing as load-slot.
bool LoadOblSave(const std::string& path)
{
    ForgetBuildingEvents();
    game::Game* game = Game();
    std::string error;
    if (!game || !game::QueueOblSave(os::ResolvePath(path), error)) {
        OBL_LOG("autotest: FAILED load-obl-save %s: %s", path.c_str(), game ? error.c_str() : "no game");
        return false;
    }
    OBL_LOG("autotest: load-obl-save %s", path.c_str());
    game->m_nextState = game->m_states[static_cast<int>(game::GameStateId::MainMenu)].get();
    NotifyWorldReloading();
    return true;
}

bool LoadOldSaveFile(const std::string& path)
{
    // The game reads no old bytes itself (save-switch.md): the file is converted as the first start converts the
    // player's saves, and the obl.save loads through the bridge.
    const std::string converted = "testlogs/old_saves/" + std::filesystem::path(path).stem().string() + ".obl.save";
    std::error_code error;
    std::filesystem::create_directories("testlogs/old_saves", error);
    return ConvertSave(path, converted) && LoadOblSave(converted);
}

void CaptureBuildingEvents()
{
    g_savedEvents.clear(); // keeps an expected load index (event-load-index) set before the save
    object::Manager* manager = game::GetObjectManager();
    if (!manager) return;
    for (uint32_t i = 0; i < manager->m_buildings.size(); ++i) {
        auto* building = static_cast<object::Building*>(manager->m_buildings[i]);
        if (building && building->m_events)
            g_savedEvents[building->m_events->m_OwnerID] = {State(building->m_events, false), building->m_events->m_CurrentSlot};
    }
}

// The game's obl.save writer (game::WriteOblSave) on the live world, with serial-save's fixed save time so the file
// does not depend on the wall clock.
bool SaveObl(const std::string& path)
{
    std::string error;
    size_t objectCount = 0;
    if (!World() || !game::WriteOblSave(os::ResolvePath(path), 1790841600u, error, &objectCount)) {
        OBL_LOG("autotest: FAILED save-obl %s: %s", path.c_str(), World() ? error.c_str() : "no world");
        return false;
    }
    // native-objects: the objects written by their family's native writer (a test expects its family present).
    OBL_LOG("autotest: save-obl %s objects=%zu", path.c_str(), objectCount);
    return true;
}

bool IsOblSaveFile(const std::string& path)
{
    // An old save starts with its binary header records; a document starts with `{` (after optional whitespace).
    std::ifstream in(os::ResolvePath(path), std::ios::binary);
    char c = 0;
    while (in.get(c) && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {}
    return in && c == '{';
}

void ForgetBuildingEvents()
{
    g_savedEvents.clear();
    g_expectedLoadIndex = -1;
}

void ResetBuildingEvents()
{
    g_savedSlot = -1;
    ForgetBuildingEvents();
}

bool ExecuteBuildingEventCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "event-build" && name != "event-state" && name != "hour" && name != "event-guests" && name != "event-slot" &&
            name != "save-panel" && name != "save-confirm" && name != "load-slot" && name != "convert-save" && name != "load-obl-save" &&
            name != "save-obl" && name != "event-expect" && name != "event-load-index" && name != "event-focus" && name != "event-tick" &&
            name != "event-select" && name != "event-rotate" && name != "event-cancel" && name != "event-advance" &&
            name != "event-visible" && name != "event-check-actors" && name != "event-delay" && name != "event-open" &&
            name != "event-normalize" && name != "event-synthetic")
        return false;
    if (name == "load-slot" && args.size() == 1) {
        LoadSaveSlot(args[0]); // works without a world (before the first frame of gameplay)
        return true;
    }
    if (name == "convert-save" && args.size() == 2) {
        ConvertSave(args[0], args[1]);
        return true;
    }
    if (name == "save-obl" && args.size() == 1) {
        SaveObl(args[0]);
        return true;
    }
    if (name == "load-obl-save" && args.size() == 1) {
        LoadOblSave(args[0]);
        return true;
    }
    layout::World* world = World();
    if (!world) {
        OBL_LOG("autotest: FAILED %s requires a world", name.c_str());
        return true;
    }
    if (name == "event-load-index" && args.size() == 1) {
        g_expectedLoadIndex = std::stoi(args[0]);
    } else if (name == "hour" && args.size() == 1) {
        float hour = std::stof(args[0]);
        float elapsed = world->m_gameTime;
        float days = world->m_gameDay + world->m_gameDayTime / 24.0f;
        if (hour < 0 || hour >= 24 || days <= 0) return true;
        float dayLength = elapsed / days;
        world->m_gameTime = dayLength * (world->m_gameDay + hour / 24.0f);
        world->m_gameDayTime = hour;
        OBL_LOG("autotest: hour %.3f dayLength=%.3f", hour, dayLength);
    } else if (name == "save-panel" || name == "save-confirm") {
        if (name == "save-panel") {
            g_savedSlot = 0;
            while (save_slots::SlotFileExists(g_savedSlot)) ++g_savedSlot;
            OBL_LOG("autotest: new save slot %d", g_savedSlot);
        }
        auto action = name == "save-panel" ? gui::gui_action_entries::SaveGamePanel : gui::gui_action_entries::SaveGameConfirm;
        if (name == "save-confirm") CaptureBuildingEvents();
        action(0);
    } else if (name == "event-build" && args.size() == 2 && !Stage()) {
        auto building = SpawnBuilding("STAGE", std::stoi(args[0]), std::stoi(args[1]));
        if (!building) return true;
        building->m_state = 1;
        building->Building::OnExitState();
        // Slot 4 initializes STATE_STARTUP (1); event simulation runs in STATE_NULL (0).
        building->m_state = 0;
        object::BuildingOpen(building);
        OBL_LOG("autotest: event-build id=%d", building->m_id);
    } else if (auto building = Stage()) {
        object::BuildingEvents* events = building->m_events;
        if (!events) {
            OBL_LOG("autotest: FAILED stage has no events");
            return true;
        }
        if (name == "event-normalize" && args.empty()) {
            for (auto& slot : events->m_slots) {
                slot.m_NumActors = 0;
                slot.m_NumGuests = 0;
            }
            // event-build bypasses construction: this fixture's job keeps its work points at (0, 0), whatever the footprint
            // collected, so the hash does not depend on them.
            for (const auto& job : world->m_constructionManager->jobs)
                if (job->m_buildingId == static_cast<int>(building->m_id))
                    for (auto& point : job->workPoints) {
                        point.x = 0;
                        point.y = 0;
                    }
        } else if ((name == "event-select" || name == "event-synthetic") && args.size() == 2) {
            int index = std::stoi(args[0]);
            int id = -1;
            for (int i = 0; i < game::GetWorldParams()->m_numEvents; ++i)
                if (ascii::EqualsNoCase(args[1], game::GetWorldParams()->m_events[i].m_key)) id = i;
            if (name == "event-synthetic") {
                // Scenario 1 has no AEROBICS definition. Exercise its real Update branch with a fixture
                // in unused table capacity; leave the parser's count and the EVENT building bug intact.
                auto* params = game::GetWorldParams();
                id = tables::WorldParams::kMaxEvents - 1;
                if (params->m_numEvents >= id) {
                    Check(false, "synthetic event capacity");
                    return true;
                }
                params->m_events.resize(static_cast<size_t>(id) + 1);
                params->m_events[id] = params->m_events[0];
                params->m_events[id].m_key = args[1];
            }
            if (index < 0 || index >= 3 || id < 0)
                Check(false, "event-select arguments");
            else {
                events->SetSlotEvent(index, id);
                events->m_CurrentSlot = index;
            }
        } else if (name == "event-rotate" && args.size() == 1) {
            static_cast<ui::SpriteInstance*>(building->m_sprite)->m_direction = std::stoi(args[0]);
        } else if (name == "event-visible" && args.size() == 1) {
            if (std::stoi(args[0]))
                events->ShowActors();
            else
                events->HideActors();
        } else if (name == "event-cancel" || name == "event-advance") {
            auto userClosed = building->m_userClose;
            if (name == "event-cancel" && args.size() == 1) building->m_userClose = std::stoi(args[0]) != 0;
            if (name == "event-cancel")
                events->CancelCurrent();
            else
                events->AdvanceSlot();
            building->m_userClose = userClosed;
        } else if (name == "event-open" && args.size() == 1) {
            building->m_userClose = std::stoi(args[0]) ? 0 : 1;
            (std::stoi(args[0]) ? object::BuildingOpen : object::BuildingClose)(building);
        } else if (name == "event-delay" && args.size() == 1) {
            messages::Message message{events->m_OwnerID, -1, messages::MT_EVENT_STARTING, 0, 0};
            events->PostToGuestsDelayed(&message, std::stof(args[0]));
            const auto& slot = events->m_slots[events->m_CurrentSlot];
            Check(slot.m_NumGuests == 0 ? message.receiver == -1 : message.receiver == slot.m_guests[slot.m_NumGuests - 1],
                    "delayed-message-receiver-mutation");
        } else if (name == "event-check-actors" && args.size() == 3) {
            const auto& slot = events->m_slots[events->m_CurrentSlot];
            bool ok = slot.m_actors.size() == std::stoul(args[0]);
            for (int id : Ids(slot.m_actors)) {
                auto* object = object::object_manager::ObjectById(game::GetObjectManager(), id);
                ok = ok && object && int(object->m_type) == std::stoi(args[1]) && int(object->m_visible) == std::stoi(args[2]);
            }
            Check(ok, "actors-count-type-visibility");
        } else if (name == "event-tick" && args.empty()) {
            // Test strict hour boundaries without advancing the world update's clock first.
            events->Update();
        } else if (name == "event-focus") {
            ui::GetMapView()->SetCameraPos(
                    static_cast<int>(building->m_WorldXYZ[0]) - 400, static_cast<int>(building->m_WorldXYZ[1]) - 450);
        } else if (name == "event-guests" && args.size() == 1) {
            object::Manager* manager = game::GetObjectManager();
            int count = std::stoi(args[0]);
            for (int i = 0; i < count && i < static_cast<int>(manager->m_guests.size()); ++i) events->AddGuest(manager->m_guests[i]->m_id);
        } else if (name == "event-expect" && args.size() == 4) {
            bool ok = Matches(events, args);
            OBL_LOG("autotest: %s event-expect", ok ? "PASS" : "FAILED");
        } else if (name == "event-slot" && args.size() == 3) {
            int slot = std::stoi(args[0]);
            if (slot >= 0 && slot < 3) {
                events->m_slots[slot].m_start = std::stof(args[1]);
                events->m_slots[slot].m_duration = std::stof(args[2]);
            }
        }
        Trace(name.c_str(), events);
    } else {
        OBL_LOG("autotest: FAILED %s requires a stage", name.c_str());
    }
    return true;
}

bool RegisterBuildingEventTrace()
{
    object::SetBuildingEventsCallObserver(&ObserveCall);
    object::SetBuildingEventsLoadObserver(&Load);
    return true;
}
} // namespace obl::test
