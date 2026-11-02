#pragma once

#include "save_slots.h"

#include <istream>
#include <string>

namespace obl::save_slots {
// The slot list's summary of a `save<N>.obl.save` (saves.md "header"): parses the document only up to the end of its `header`
// object, so listing a folder of saves does not read whole worlds (save-switch.md). The document must start with
// `"format": "obl.save"` and `"version": 1`; false for anything else, a missing header field or invalid JSON before the
// header ends. `m_number` and `m_oblSave` are the caller's.
bool ReadOblHeader(std::istream& in, Slot& slot);

// `save<N>.obl.save` beside the old `save<N>.dat` (save_slots.h SlotPath).
std::string OblSlotPath(int number);
// The file of a listed slot: its obl.save or, until the old read path goes (save-switch.md), its old save.
std::string SlotPath(const Slot& slot);
// Whether the slot number is used by either file kind (a test picking a free scratch number must avoid both).
bool SlotFileExists(int number);
} // namespace obl::save_slots
