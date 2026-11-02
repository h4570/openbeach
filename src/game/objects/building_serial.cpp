#include "core/text16.h"
#include "game/objects/building_serial.h"
#include "game/objects/building_events_state.h"
#include "game/objects/building_queue.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/objects/building_queries.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/building_thoughts.h"
#include "game/construction/construction_manager.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/objects/building_methods.h"
#include "game/world/save_objects_load.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// The game side of a building's obl.save load (docs/architecture/objects.md): the events, queue and thought-list steps and the thought
// fixture of the tests.
namespace obl::object {
using game::GetObjectManager;
using game::GetWorld;

namespace {

using object::Building;
using object::ThoughtEntry;
using ui::SpriteInstance;

test::CallCounter g_thoughtFixture("objects.building-serial-thought-fixture");

// What the obl.save load of a building makes by hand: the events and queue blocks and the thought list.
struct BuildEnv {
    void* ConstructEvents() { return object::CreateBuildingEvents(new object::BuildingEvents, 0); }
    void* ConstructQueue(Building& b)
    {
        return BuildingQueueConstruct(new object::BuildingQueue, static_cast<uint32_t>(b.m_definition->m_MaxQueue), b.m_serve_point[0],
                b.m_serve_point[1], b.m_queue_direction[0], b.m_queue_direction[1], static_cast<uint32_t>(b.m_id));
    }
};

} // namespace

void BuildingLoadEventsJson(Building& building, const world_save::BuildingEventsJson& in)
{
    BuildEnv env;
    building.m_events = static_cast<object::BuildingEvents*>(env.ConstructEvents());
    building.m_events->LoadSaved(in);
}

void BuildingLoadQueueJson(Building& building, const world_save::BuildingQueueJson& in)
{
    BuildEnv env;
    building.m_queue = env.ConstructQueue(building);
    BuildingQueueLoadJson(building.m_queue, in);
}

void BuildingLoadThoughtsJson(Building& building, const std::vector<world_save::BuildingThoughtJson>& in)
{
    thought_ops::Clear(building.m_ThoughtList);
    for (const auto& thought : in) {
        auto* entry = static_cast<ThoughtEntry*>(::operator new(sizeof(ThoughtEntry)));
        entry->m_objectId = thought.object;
        const size_t length = thought.text.size() + 1;
        entry->m_text = static_cast<Char16*>(::operator new(length * sizeof(Char16)));
        for (size_t i = 0; i + 1 < length; ++i) entry->m_text[i] = static_cast<Char16>(thought.text[i]);
        entry->m_text[length - 1] = 0;
        building.m_ThoughtList.push_back(entry);
    }
}

namespace {

// ---- drivers ---------------------------------------------------------------------------------------------------------------------------

std::vector<Building*> Buildings()
{
    std::vector<Building*> buildings;
    object::Manager* manager = GetObjectManager();
    if (GetWorld() && manager)
        for (uint32_t i = 0; i < manager->m_buildings.size(); ++i)
            if (manager->m_buildings[i]) buildings.push_back(static_cast<Building*>(manager->m_buildings[i]));
    return buildings;
}

// `call-drive objects.building-serial-thought-fixture N`: gives every building thoughts. Below 16 they go through AddThought (the list keeps the last eight);
// from 16 up, N entries are pushed to the front by hand, so that the list is longer than eight (save and load keep every entry).
void PushThoughtFront(Building& building, const Char16* text, int32_t guestId)
{
    auto* entry = static_cast<ThoughtEntry*>(::operator new(sizeof(ThoughtEntry)));
    entry->m_objectId = guestId;
    const size_t length = text16::Length(text);
    entry->m_text = static_cast<Char16*>(::operator new((length + 1) * sizeof(Char16)));
    text16::Copy(entry->m_text, text);
    building.m_ThoughtList.push_front(entry);
}

void DriveThoughtFixture(int count)
{
    Char16 text[24] = u"fixture thought";
    int guest = 0;
    for (Building* building : Buildings()) {
        for (int i = 0; i < count; ++i) {
            text[15] = static_cast<Char16>(u'a' + (i + guest) % 26);
            text[16] = i % 5 ? 0 : static_cast<Char16>(u'!'); // different lengths
            text[17] = 0;
            if (count >= 16)
                PushThoughtFront(*building, text, 100 + i);
            else
                BuildingAddThought(building, text, 100 + i);
        }
        ++guest;
        g_thoughtFixture.Count();
    }
}

} // namespace

bool RegisterBuildingThoughtDriver()
{
    g_thoughtFixture.SetDriver(&DriveThoughtFixture);
    return true;
}

} // namespace obl::object

namespace obl::object {} // namespace obl::object
