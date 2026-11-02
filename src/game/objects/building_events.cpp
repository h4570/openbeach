#include "game/world/world_receive.h"
#include "game/objects/object_base.h"
#include "game/map/grid_state.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/building_events.h"
#include "game/objects/building_events_trace.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/world/world_params.h"
#include "game/objects/building_methods.h"
#include "game/world/save_objects_load.h"

namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

namespace {

using messages::Message;
using object::BuildingEvents;
using object::EventSlot;
using MessageHandler = messages::Handler;
using IdDeque = std::deque<int32_t>;
BuildingEventsLoadObserver g_observer;
BuildingEventsCallObserver g_callObserver;

EventSlot& Current(BuildingEvents* events)
{
    return events->m_slots[events->m_CurrentSlot];
}
MessageHandler* Handler()
{
    return messages::message_facade::Current();
}
object::Base* Object(int id)
{
    return object_manager::ObjectById(GetObjectManager(), id);
}
object::Building* Owner(BuildingEvents* events)
{
    return static_cast<object::Building*>(Object(events->m_OwnerID));
}
const tables::EventDef* Definition(int id)
{
    // The lookup keeps the EVENT table bug and its unchecked -1 fallback: event references depend on it.
    return world::world_params::GetEventDefinition(GetWorldParams(), id);
}
void Post(BuildingEvents* events, int receiver, int type, int flags = 0)
{
    Message message{events->m_OwnerID, receiver, type, flags, 0};
    messages::message_facade::Post(Handler(), &message);
}
void PostDelayed(BuildingEvents* events, int receiver, int type, float delay)
{
    Message message{events->m_OwnerID, receiver, type, 0, 0};
    messages::message_facade::PostDelayed(Handler(), &message, delay);
}

// The loops run to the stored counters, which a save keeps apart from the deques (and m_NumActors is uninitialized before the first
// show). An index past the deque is skipped instead of reading outside it.
bool IdAt(const IdDeque& deque, int i, int32_t& id)
{
    if (i < 0 || static_cast<size_t>(i) >= deque.size()) return false;
    id = deque[static_cast<size_t>(i)];
    return true;
}
void SetEvent(BuildingEvents* events, int index, int id)
{
    auto& slot = events->m_slots[index];
    slot.m_EventID = id;
    const auto* def = Definition(id);
    slot.m_intensity[0] = def->m_intensity[0] * events->m_intensityMultiplier;
    slot.m_intensity[1] = def->m_intensity[1] * events->m_intensityMultiplier;
    slot.m_price = def->m_price;
    slot.m_cost = def->m_cost;
}
BuildingEvents* Create(BuildingEvents* events, int owner)
{
    events->m_OwnerID = owner;
    events->m_eventActive = 0;
    events->m_intensityMultiplier = 1.0f;
    events->m_CurrentSlot = 0;
    int id = -1;
    for (uint32_t i = 0; i < static_cast<uint32_t>(GetWorldParams()->m_numEvents); ++i) {
        if (Definition(i)->m_available) {
            id = i;
            break;
        }
    }
    constexpr float starts[]{9.0f, 13.0f, 16.0f};
    for (int i = 0; i < 3; ++i) {
        auto& slot = events->m_slots[i];
        slot.m_start = starts[i];
        slot.m_duration = 2.5f;
        SetEvent(events, i, id);
        slot.m_NumGuests = 0; // NumActors deliberately remains uninitialized (the save keeps it apart).
    }
    events->m_CurrentSlot = building_events::ChooseSlot(*events, GetWorld()->m_gameDayTime);
    return events;
}
void Advance(BuildingEvents* events)
{
    for (int i = 0, id = 0; i < Current(events).m_NumGuests; ++i)
        if (IdAt(Current(events).m_guests, i, id)) PostDelayed(events, id, messages::MT_EVENT_FINISHED, 2.0f);
    Current(events).m_guests.clear();
    Current(events).m_NumGuests = 0;
    if (++events->m_CurrentSlot == 3) events->m_CurrentSlot = 0;
    events->m_eventActive = 0;
}
void Cancel(BuildingEvents* events)
{
    for (int i = 0, id = 0; i < Current(events).m_NumActors; ++i)
        if (IdAt(Current(events).m_actors, i, id)) Object(id)->m_waitingForDeath = 1;
    Current(events).m_actors.clear();
    Current(events).m_NumActors = 0;
    for (int i = 0, id = 0; i < Current(events).m_NumGuests; ++i) {
        if (!IdAt(Current(events).m_guests, i, id)) continue;
        bool closed = BuildingIsUserOrPowerClosedDuringHours(Owner(events));
        Post(events, id, messages::MT_EVENT_FINISHED, closed ? 1 : 0);
    }
    Current(events).m_guests.clear();
    Current(events).m_NumGuests = 0;
    events->m_eventActive = 0;
}
void Visibility(BuildingEvents* events, uint8_t visible)
{
    // Visibility follows the actual deque size, unlike Update/Cancel's separately stored counters.
    for (size_t i = 0; i < Current(events).m_actors.size(); ++i)
        if (auto* object = Object(Current(events).m_actors[i])) object->m_visible = visible;
}
void DelayGuests(BuildingEvents* events, Message* message, float delay)
{
    for (int i = 0, id = 0; i < Current(events).m_NumGuests; ++i) {
        if (!IdAt(Current(events).m_guests, i, id)) continue;
        message->receiver = id;
        float jitter = building_events::GuestDelay(Rand(), delay);
        messages::message_facade::PostDelayed(Handler(), message, jitter);
    }
}
void SpawnActor(BuildingEvents* events, int type, int variant)
{
    auto* building = Owner(events);
    auto* sprite = static_cast<ui::SpriteInstance*>(building->m_sprite);
    int tile[2];
    building->GetTilePosition(tile);
    const grid_math::PathPoint pos = GetGrid()->GetFootprintOffsetPoint(tile[0], tile[1], building->m_definition->m_GridWidth,
            building->m_definition->m_GridHeight, -122.0f, -286.0f, sprite->m_direction);
    object::BuildingEventActorParams actor{variant, {pos.x, pos.y, 0.0f}, events->m_OwnerID};
    Message command{-2, -2, 0, 0, type};
    world::PostPlaceCommand(command, &actor);
}
void Update(BuildingEvents* events)
{
    float hour = GetWorld()->m_gameDayTime;
    if (events->m_eventActive) {
        if (!building_events::PastEnd(Current(events), hour)) return;
        for (int i = 0, id = 0; i < Current(events).m_NumActors; ++i)
            if (IdAt(Current(events).m_actors, i, id)) Post(events, id, messages::MT_EVENT_FINISHED);
        Current(events).m_actors.clear();
        Current(events).m_NumActors = 0;
        return; // The actor's later reply advances the slot, not this branch.
    }
    if (!building_api::IsOpen(Owner(events))) {
        if (building_events::PastEnd(Current(events), hour) && static_cast<double>(hour) - Current(events).m_start < 5.0)
            Post(events, events->m_OwnerID, messages::MT_EVENT_FINISHED);
        return;
    }
    if (!building_events::CanStart(Current(events), hour)) return;
    Current(events).m_NumActors = 0;
    auto cast = building_events::GetCast(Definition(Current(events).m_EventID)->m_key.c_str());
    for (int i = 0; i < cast.count; ++i) SpawnActor(events, cast.type, cast.randomVariant ? Rand() % 2 : i);
    for (int i = 0, id = 0; i < Current(events).m_NumGuests; ++i)
        if (IdAt(Current(events).m_guests, i, id)) PostDelayed(events, id, messages::MT_EVENT_STARTING, 2.0f);
    events->m_eventActive = 1; // Unknown event keys also become active, without an actor.
}

void Observe(BuildingEventsCall call, BuildingEvents* events, bool after)
{
    if (g_callObserver) g_callObserver(call, events, after);
}
} // namespace

} // namespace obl::object

namespace obl::object {
using object::BuildingEventsCall;

BuildingEvents* CreateBuildingEvents(BuildingEvents* memory, int owner)
{
    return object::Create(memory, owner);
}
void DestroyBuildingEvents(BuildingEvents* events)
{
    delete events;
}
void BuildingEvents::Update()
{
    object::Observe(BuildingEventsCall::Update, this, false);
    object::Update(this);
    object::Observe(BuildingEventsCall::Update, this, true);
}
void BuildingEvents::AdvanceSlot()
{
    object::Observe(BuildingEventsCall::AdvanceSlot, this, false);
    object::Advance(this);
    object::Observe(BuildingEventsCall::AdvanceSlot, this, true);
}
void BuildingEvents::CancelCurrent()
{
    object::Cancel(this);
}
void BuildingEvents::ShowActors()
{
    object::Visibility(this, 1);
}
void BuildingEvents::HideActors()
{
    object::Visibility(this, 0);
}
void BuildingEvents::AddActor(int id)
{
    object::Current(this).m_actors.push_back(id);
    ++object::Current(this).m_NumActors;
}
void BuildingEvents::AddGuest(int id)
{
    object::Current(this).m_guests.push_back(id);
    ++object::Current(this).m_NumGuests;
}
void BuildingEvents::SetSlotEvent(int index, int id)
{
    object::SetEvent(this, index, id);
}
void BuildingEvents::PostToGuestsDelayed(messages::Message* message, float delay)
{
    object::DelayGuests(this, message, delay);
}

// The current slot is not saved, so it is chosen again from the time of day.
void BuildingEvents::LoadSaved(const world_save::BuildingEventsJson& in)
{
    m_OwnerID = in.owner;
    m_eventActive = in.active;
    for (int i = 0; i < 3; ++i) {
        const auto& from = in.slots[i];
        object::SetEvent(this, i, from.event);
        auto& slot = m_slots[i];
        slot.m_NumActors = static_cast<int32_t>(from.actorCount);
        slot.m_actors.insert(slot.m_actors.end(), from.actors.begin(), from.actors.end());
        slot.m_NumGuests = static_cast<int32_t>(from.guestCount);
        slot.m_guests.insert(slot.m_guests.end(), from.guests.begin(), from.guests.end());
        slot.m_start = from.start;
        slot.m_duration = from.duration;
        slot.m_price = from.price;
    }
    if (game::GetWorld()) {
        int index = building_events::ChooseSlot(*this, game::GetWorld()->m_gameDayTime);
        if (index != m_CurrentSlot) OBL_LOG("building events: restored slot index %d -> %d", m_CurrentSlot, index);
        m_CurrentSlot = index;
    }
    if (object::g_observer) object::g_observer(this);
}
int BuildingEvents::GetActiveEventId()
{
    return m_eventActive ? object::Current(this).m_EventID : -1;
}
int BuildingEvents::GetSlotEventId(int index) const
{
    return m_slots[index].m_EventID;
}
bool BuildingEvents::IsSlotActive(int index) const
{
    return m_eventActive && index == m_CurrentSlot;
}
} // namespace obl::object

namespace obl::object {
void SetBuildingEventsLoadObserver(BuildingEventsLoadObserver observer)
{
    g_observer = observer;
}

void SetBuildingEventsCallObserver(BuildingEventsCallObserver observer)
{
    g_callObserver = observer;
}
} // namespace obl::object
