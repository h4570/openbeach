#pragma once

#include "game/construction/construction_state.h"

#include <cstdint>

// The ConstructionManager's save block (docs/specs/construction.md): the records, order, names
// (repeated ones included) and literal size, which the obl.save reader and writer and the converter still follow.
namespace obl::construction {

// The field table: the records after the class name in save order, for the obl.save reader and the world snapshot's
// ConstructionEmitter. A visitor provides
//   Jobs(name, jobs, fn(visitor, job))              a uint count, then fn per job (the load appends new jobs)
//   Define(job)                                     after the m_buildingId: the load makes the job from its building's definition here
//                                                   (whether it has work points), the others do nothing
//   Hoardings(name, hoardings, fn(visitor, h))      an int count, then fn per pending fence
//   Builders(count, builders, fn(visitor, builder)) the first `count` entries of the table (no record of its own)
//   Int(name, int32_t&)  UInt(name, uint32_t&)  Float(name, float&)  Bool(name, uint8_t&) on a raw byte
// The ten work points are written for every job; a job without work points writes zeros (the saver wrote the unallocated
// points as zeros too).
template<class M, class Visitor> void VisitFields(M& manager, Visitor& v)
{
    v.Jobs("jobs", manager.jobs, [](Visitor& v, auto& job) {
        v.Int("building", job.m_buildingId);
        v.Define(job);
        v.Int("builders", job.m_builderCount);
        v.Hoardings("hoardings", job.hoardings, [](Visitor& v, auto& hoarding) {
            v.Int("dir", hoarding.m_direction);
            v.Int("x", hoarding.x);
            v.Int("y", hoarding.y);
            v.Int("extra", hoarding.m_savedMetadata);
        });
        // The saved names repeat (m_constructionRemaining, m_BuildingReady): they label the start/done times, the progress and both flags.
        v.Float("construction_start", job.m_constructionStart);
        v.Float("demolition_start", job.m_demolitionStart);
        v.Float("construction_done", job.m_constructionRemaining);
        v.Float("demolition_done", job.m_demolitionRemaining);
        v.Float("construction_progress", job.m_constructionProgress);
        v.Float("demolition_progress", job.m_demolitionProgress);
        v.Bool("under_construction", job.m_underConstruction);
        v.Bool("under_demolition", job.m_underDemolition);
        v.Bool("started", job.m_started);
        v.Bool("finished", job.m_finished);
        for (auto& point : job.workPoints) {
            v.UInt("work_points[].x", point.x);
            v.UInt("work_points[].y", point.y);
            v.Int("work_points[].dir", point.direction);
        }
    });
    v.Int("constructions", manager.m_constructionCount);
    v.Int("builders", manager.m_builderCount);
    v.Builders(manager.m_builderCount, manager.m_builders, [](Visitor& v, auto& builder) {
        v.Int("staff", builder.m_builderId);
        v.Int("status", builder.m_status);
        v.UInt("location", builder.m_location);
        v.Int("building", builder.m_buildingId);
        v.Bool("manual", builder.m_manuallyAssigned);
    });
}

// Replaces the job list; the builder table keeps the entries past the loaded count. Used by the obl.save reader
// (game/world/save_table_reader.h).
template<class Visitor> void LoadFields(Manager& manager, Visitor& visitor)
{
    manager.jobs.clear();
    VisitFields(manager, visitor);
    // A job without work points drops what the file holds for them (the load discards them), so they stay zero.
    for (const auto& job : manager.jobs)
        if (!job->hasWorkPoints) job->workPoints = {};
}

} // namespace obl::construction
