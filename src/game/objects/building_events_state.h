#pragma once

#include <cstdint>
#include <deque>

namespace obl::messages {
struct Message;
} // namespace obl::messages
namespace obl::world_save {
struct BuildingEventsJson;
} // namespace obl::world_save

// C++ building state; compatibility offsets remain in the layout wrapper.
namespace obl::object {

struct EventSlot {
    std::deque<int32_t> m_actors;
    int32_t m_NumActors; // uninitialized by the constructor until the first show: ask the deque instead
    std::deque<int32_t> m_guests;
    int32_t m_NumGuests; // zeroed by event schedule initialization, saved independently of deque size (see IdAt in building_events.cpp)
    float m_start; // hour of the day
    float m_duration; // game hours
    float m_price; // event definition price
    float m_cost; // event definition cost
    float m_intensity[2]; // male, female: definition intensities times the intensity multiplier
    int32_t m_EventID; // WorldParams event definition
};

struct BuildingEvents {
    EventSlot m_slots[3];
    int32_t m_OwnerID; // owning building
    uint8_t m_eventActive;
    float m_intensityMultiplier; // starts at 1.0
    int32_t m_CurrentSlot; // 0..2; not saved, LoadSaved chooses it again

    // The game-bound operations (building_events.cpp).
    void Update();
    void AdvanceSlot();
    void CancelCurrent();
    void ShowActors();
    void HideActors();
    void AddActor(int id);
    void AddGuest(int id);
    void SetSlotEvent(int index, int id);
    void PostToGuestsDelayed(messages::Message* message, float delay);
    // The fields of an obl.save `building.events` record, then the current slot from the time of day.
    void LoadSaved(const world_save::BuildingEventsJson& in);
    int GetActiveEventId(); // -1 when no event runs
    int GetSlotEventId(int index) const;
    bool IsSlotActive(int index) const;
};

// Builds the events of building `owner` in `memory` (a `new BuildingEvents`); DestroyBuildingEvents deletes it.
BuildingEvents* CreateBuildingEvents(BuildingEvents* memory, int owner);
void DestroyBuildingEvents(BuildingEvents* events);

struct BuildingEventActorParams {
    int32_t m_variant;
    float m_position[3];
    int32_t m_owner;
};

} // namespace obl::object
