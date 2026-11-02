#pragma once

#include "game/objects/building_events_state.h"

// Observers of the building events for the autotest trace (src/testing/building_events.cpp).
namespace obl::object {

// Called after every load of the events with the events object.
using BuildingEventsLoadObserver = void (*)(object::BuildingEvents* events);
void SetBuildingEventsLoadObserver(BuildingEventsLoadObserver observer);

// Called before (`after` false) and after Update and AdvanceSlot.
enum class BuildingEventsCall { Update, AdvanceSlot };
using BuildingEventsCallObserver = void (*)(BuildingEventsCall call, object::BuildingEvents* events, bool after);
void SetBuildingEventsCallObserver(BuildingEventsCallObserver observer);

} // namespace obl::object
