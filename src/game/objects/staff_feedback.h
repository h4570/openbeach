#pragma once

namespace obl::object {

struct Rep;
} // namespace obl::object

namespace obl::object {

// The staff's own methods whose logic lives in src/common (staff_update.h, staff_update_feedback.h, staff_receive.h, staff_profession_receive.h,
// staff_state.h): the staff Update/ReceiveMessage/OnExitState and, per profession, Update, UpdateFeedback, ReceiveMessage, Sack and OnEnterState.
// The classes' virtuals call them (no image install).

// The rep's UpdateFeedback (for the rep's CallFunction).
void RepUpdateFeedback(object::Rep& rep);

} // namespace obl::object
