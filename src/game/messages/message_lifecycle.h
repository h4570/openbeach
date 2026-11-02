#pragma once

#include "game/messages/message_state.h"

#include <cstdint>

namespace obl::messages {

// The field table: the records after the class name in save order, for the obl.save reader and the world snapshot's
// MessageEmitter. A visitor provides
//   List(name, list, fn(visitor, element))      a uint count, then fn per element (the load appends default elements)
//   Int(name, int32_t&)  Float(name, float&)
//   Payload(name, int32_t&) / Payload(name, float&)   saved, but left out of the snapshot (the meaning depends on the type, and not
//                                               every sender fills them)
template<class H, class Visitor> void VisitFields(H& handler, Visitor& v)
{
    v.List("delayed", handler.delayed, [](Visitor& v, auto& m) {
        v.Int("sender", m.sender);
        v.Int("receiver", m.receiver);
        v.Int("type", m.type);
        v.Int("flags", m.flags);
        v.Int("data", m.data);
        v.Payload("float_data", m.floatData);
        v.Float("time", m.time);
        v.Payload("extra", m.extra);
    });
}

// Replaces the delayed list; the working list is empty between frames and stays as it is. Used by the obl.save reader (game/world/save_table_reader.h).
template<class Visitor> void LoadFields(Handler& handler, Visitor& visitor)
{
    handler.delayed.clear();
    VisitFields(handler, visitor);
}

} // namespace obl::messages
