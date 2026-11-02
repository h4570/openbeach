#pragma once

#include <cstdint>

namespace obl::object {
struct Rep;
struct SecurityGuard;
} // namespace obl::object

namespace obl::ui {

// The representative's LookForGuests / ArrivedAtGuest / MoveToNextBuilding and the security guard's LookForOffender / ChooseAction:
// the helpers their scripts call through Rep::CallFunction / SecurityGuard::CallFunction (src/game/objects/staff_rep_security.h), counted as `objects.rep-security-actions`; a call with an input they cannot handle ends in fatal::UnhandledInput. The point
// argument of LookForOffender is dropped (the game never reads it).
void RepLookForGuests(object::Rep& rep);
void RepArrivedAtGuest(object::Rep& rep);
void RepMoveToNextBuilding(object::Rep& rep);
uint8_t SecurityLookForOffender(object::SecurityGuard& guard);
void SecurityChooseAction(object::SecurityGuard& guard);

} // namespace obl::ui
