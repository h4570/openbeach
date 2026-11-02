#pragma once

namespace obl::object {

// The script variables and built-in functions of a guest (guest script variables and built-in functions, docs/architecture/objects.md): the dispatchers `GetValue`, `SetValue` over src/game/objects/guest_script_values.h, and `CallFunction`. A number the
// guest does not answer goes on to the base class (the human `GetValue/SetValue`), and the functions that are not inline in the
// `switch` are the guest's own member functions.
bool RegisterGuestScriptDelegates();

} // namespace obl::object
