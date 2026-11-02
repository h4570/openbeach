#pragma once

#include <cstdint>

namespace obl::object {

struct Lifeguard;
struct Mechanic;
struct Rep;
} // namespace obl::object

namespace obl::object {
// The lifeguard's Leave, the mechanic's ReturnJobAndLeave and the rep's ReleaseGuestAndLeave (counted as `objects.staff-actions`).
void LifeguardLeave(object::Lifeguard& employee);
void MechanicReturnJobAndLeave(object::Mechanic& employee, bool found);
void RepReleaseGuestAndLeave(object::Rep& employee, const uint32_t* guestId, bool sent);
} // namespace obl::object
