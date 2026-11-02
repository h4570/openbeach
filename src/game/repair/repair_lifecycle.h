#pragma once

#include "game/repair/repair_state.h"

#include <cstdint>
#include <initializer_list>

namespace obl::repair {

// The field table: the records after the class name in save order, for every consumer (the obl.save reader, the world
// snapshot's RepairEmitter, the test dump's RepairDump). A visitor provides
//   List(name, list, fn(visitor, element))      a uint count, then fn per element (the load appends default elements)
//   Int(name, int32_t&)  UInt(name, uint32_t&)  Bool(name, uint8_t&) (a raw byte)  Float(name, float&)
//   Scratch(name, float&)                       a float the save keeps but the snapshot leaves out (the scan distance)
template<class M, class Visitor> void VisitFields(M& m, Visitor& v)
{
    auto job = [](Visitor& v, auto& j) {
        v.Int("object", j.id);
        v.UInt("calls", j.calls);
        v.Scratch("distance", j.distance);
        v.Float("time_called", j.timeCalled);
        v.Bool("urgent", j.urgent);
        v.Int("type", j.type);
    };
    v.List("building_jobs", m.buildingJobs, job);
    v.List("scenery_jobs", m.sceneryJobs, job);
    v.List("active_jobs", m.activeJobs, [](Visitor& v, auto& j) {
        v.Int("object", j.id);
        v.Bool("repairing", j.repairing);
        v.Bool("urgent", j.urgent);
        v.Int("type", j.type);
    });
    v.Int("breakdowns", m.breakdownCount);
    v.Int("vandalisms", m.vandalismCount);
    v.Int("active_count", m.activeCount);
}

// Replaces the three lists and the counters. Used by the obl.save reader (game/world/save_table_reader.h).
template<class Visitor> void LoadFields(Manager& manager, Visitor& visitor)
{
    manager.buildingJobs.clear();
    manager.sceneryJobs.clear();
    manager.activeJobs.clear();
    VisitFields(manager, visitor);
}

// RemoveJob: the first pending request for the object leaves its queue, the building list first; a hit there ends the search, so a
// request queued in both lists keeps its scenery entry. The counters and the active list are not touched.
inline void RemoveJob(Manager& manager, int id)
{
    for (JobList* list : {&manager.buildingJobs, &manager.sceneryJobs}) {
        for (auto it = list->begin(); it != list->end(); ++it) {
            if (it->id != id) continue;
            list->erase(it);
            return;
        }
    }
}

// CountUrgentJobs: the breakdowns (building jobs of type 2) plus every scenery job, whatever its urgency flag says.
inline uint32_t CountUrgentJobs(const Manager& manager)
{
    uint32_t count = 0;
    for (const Job& job : manager.buildingJobs) count += job.type == 2;
    return count + static_cast<uint32_t>(manager.sceneryJobs.size());
}

} // namespace obl::repair
