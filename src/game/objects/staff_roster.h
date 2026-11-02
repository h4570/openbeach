#pragma once

#include <vector>

namespace obl::object {

struct Staff;
}

namespace obl::object {

// Staff profession services (docs/architecture/objects.md).
// staff_wages.h defines happiness targets, shift hours and severance; staff_thoughts.h and staff_lookup.h
// define feedback text, thoughts, walk targets, state support, home assignment and hover selection.
// staff_script_values.h and staff_profession_script_values.h define the shared and profession-specific script variables.
// staff_serial.h provides save diagnostics and staff_lifecycle.cpp exercises attached sprite cleanup.
// The happiness target recalculation (the handler forwards here).
void StaffRecalculateHappinessTarget(object::Staff& member);

// The first staff member of any profession of the running world (the test drivers borrow it), null if there is none.
object::Staff* FirstStaffMember();

// The first staff member of each class the world has (the class decides which profession's methods the drivers reach).
std::vector<object::Staff*> StaffOfEveryClass();

} // namespace obl::object
