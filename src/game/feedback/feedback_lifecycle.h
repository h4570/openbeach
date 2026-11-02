#pragma once

#include "feedback_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace obl::feedback {

// The field table: every record of the block after the class name, in save order. The obl.save reader, the world snapshot
// (EmitFeedback) and the managers dump (world_dump.cpp) are visitors over it, so the record list exists once. `M` is
// `Manager` or `const Manager`. A visitor provides:
//   List(name, deque, fn)         a "uint" count record, then fn(visitor, element) per element
//   Int(name, field)              an "int" record of a 4-byte field (the staff masks are uint32_t, written as int)
//   Bool(name, uint8_t field)     a "bool" record of a raw byte (a loaded 2 stays 2)
//   Float(name, field), Ints(name, std::array<uint32_t, N>), Text(text) (the "wide" flag, then "text")
template<class M, class Visitor> void VisitFields(M& m, Visitor& v)
{
    v.List("pending", m.pending, [](Visitor& v, auto& n) {
        v.Int("object", n.objectId);
        v.Text(n.text);
    });
    v.List("history", m.history, [](Visitor& v, auto& n) {
        v.Int("object", n.objectId);
        v.Int("day", n.day);
        v.Float("hour", n.hour);
        v.Text(n.text);
    });
    v.Int("cleaner_messages", m.cleanerMessages);
    v.Int("lifeguard_messages", m.lifeGuardMessages);
    v.Int("security_guard_messages", m.securityGuardMessages);
    v.Int("mechanic_messages", m.mechanicMessages);
    v.Int("rep_messages", m.repMessages);
    v.Int("builder_messages", m.builderMessages);
    v.Int("staff_messages", m.staffMessages);
    v.Int("suppressed_count", m.suppressedCount);
    v.Bool("suppress_world", m.suppressWorld);
    v.Bool("suppress_objects", m.suppressObject);
    v.Ints("suppressed", m.suppressed);
    v.Float("last_missed_desires_check", m.lastReviewHour);
}

// Replaces both queues and the fields; the history cursor is not saved and stays. Used by the obl.save reader (game/world/save_table_reader.h).
template<class Visitor> void LoadFields(Manager& m, Visitor& visitor)
{
    m.pending.clear();
    m.history.clear();
    VisitFields(m, visitor);
}

} // namespace obl::feedback
