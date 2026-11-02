#pragma once

#include "game/cleaning/cleaning_state.h"

#include <cstdint>

namespace obl::cleaning {

// The field table: the records after the class name in save order, for every consumer (the obl.save reader, the world
// snapshot's CleaningEmitter, the test dump's CleaningDump). A visitor provides
//   List(name, list, fn(visitor, element))      a uint count, then fn per element (the load appends default elements)
//   Int(name, int32_t&)  UInt(name, uint32_t&)  Bool(name, uint8_t&) (a raw byte)  Float(name, float&)
template<class M, class Visitor> void VisitFields(M& m, Visitor& v)
{
    v.List("jobs", m.jobs, [](Visitor& v, auto& j) {
        v.Int("object", j.id);
        v.UInt("calls", j.calls);
        v.Float("time_called", j.timeCalled);
        v.Bool("urgent", j.urgent);
    });
    v.List("active_jobs", m.activeJobs, [](Visitor& v, auto& j) {
        v.Int("object", j.id);
        v.Bool("cleaning", j.cleaning);
        v.Bool("urgent", j.urgent);
    });
    v.Int("cleanings", m.cleaningCount);
    v.Int("active_count", m.activeCount);
}

// Replaces both lists and the counters. Used by the obl.save reader (game/world/save_table_reader.h).
template<class Visitor> void LoadFields(Manager& manager, Visitor& visitor)
{
    manager.jobs.clear();
    manager.activeJobs.clear();
    VisitFields(manager, visitor);
}

} // namespace obl::cleaning
