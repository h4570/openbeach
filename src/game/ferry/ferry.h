#pragma once

#include "game/ferry/ferry_state.h"

#include <cstdint>

namespace obl::object {
struct Guest;
} // namespace obl::object
namespace obl::ferry {

// Registers the ferry schedule test driver.
bool RegisterFerryTestDriver();

namespace ferry_facade {

// The manager is created with new and freed with delete; it is not on the game heap.
ferry::Manager* Create();
void Destroy(ferry::Manager* manager);

void AddBoatEvent(ferry::Manager* manager, const ferry::Boat* input);
bool PopDueBoat(ferry::Manager* manager, ferry::Arrival* out);
void ResetCursor(ferry::Manager* manager);
double GetNextBoatTime(ferry::Manager* manager, int today, uint32_t* day);
double GetBoatTimeToday(ferry::Manager* manager, int index);
// `guest` is the leaving guest's object, read for its definition id, happiness and early-leave flag.
void RecordGuestLeaving(ferry::Manager* manager, object::Guest* guest);

} // namespace ferry_facade
} // namespace obl::ferry
