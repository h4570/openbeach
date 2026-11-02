#include "game/construction/construction_manager.h"

#include "game/construction/construction_jobs.h"
#include "game/construction/construction_queries.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_staff.h"
#include "game/objects/building_api.h"
#include "game/construction/construction_lifecycle.h"
#include "game/objects/object_registry_api.h"
#include "game/world/save_table_reader.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>

namespace obl::construction {
namespace {

using construction::Job;
using construction::Manager;
using object::Building;
using object::Staff;
using ui::SpriteInstance;
const auto kIsOpen = &object::building_api::IsOpen;

test::CallCounter g_callsAddBuilder("construction.add-builder.calls"), g_callsRemoveBuilder("construction.remove-builder.calls");
test::CallCounter g_answered("construction.manager");
test::CallCounter g_answeredQueries("construction.queries");

runtime::LoadedFences g_lastFences;

// The game side of the query Env (src/game/construction/construction_queries.h).
struct QueryEnv {
    static object::Base* Object(int id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    bool ObjectExists(int id) { return Object(id) != nullptr; }
    bool BuilderCanWork(int builderId)
    {
        auto* staff = static_cast<Staff*>(Object(builderId));
        if (!staff) return false;
        auto* home = static_cast<Building*>(Object(staff->m_HomeID));
        return home && staff->IsOnShift() && kIsOpen(home);
    }
    int BuildingDirection(int buildingId)
    {
        object::Base* object = Object(buildingId);
        return object && object->m_sprite ? static_cast<SpriteInstance*>(object->m_sprite)->m_direction : 0;
    }
};

// What construction::ConstructJob reads from the building. A missing object gives a job without work
// points (only a save whose building is gone asks for one).
construction::JobDefinition Definition(int buildingId)
{
    auto* building = static_cast<Building*>(object::object_manager::ObjectById(game::GetObjectManager(), buildingId));
    if (!building || !building->m_definition) return {};
    const tables::BuildingDef& def = *building->m_definition;
    return {def.m_BuildTime, def.m_DemolishTime, def.m_footprint != nullptr};
}

// ---- Group C2: the builder table --------------------------------------------------------------------------------------------------------

// The game side of the builder-table Env (src/game/construction/construction_jobs.h).
struct BuilderEnv {
    Manager* manager;
    bool BuilderToken(int builderId, int* token)
    {
        auto* object = static_cast<object::Builder*>(object::object_manager::ObjectById(game::GetObjectManager(), builderId));
        if (!object) return false;
        *token = object->m_BuilderNumber;
        return true;
    }
    void Reallocate() { runtime::ReallocateBuilders(manager); }
};

// The obl.save reader with the stream load's job step: each loaded job is made from its building's definition.
class JsonReader : public world_save::TableReader<JsonReader> {
public:
    using TableReader::TableReader;
    void Define(Job& job) { construction::ConstructJob(job, job.m_buildingId, Definition(job.m_buildingId)); }
};

// The test observation of LastLoadedFences after either load.
void RecordLoadedFences(const Manager& manager)
{
    g_lastFences = runtime::LoadedFences{};
    for (const auto& job : manager.jobs) {
        if (job->hoardings.empty()) continue;
        g_lastFences.buildId = job->m_buildingId;
        g_lastFences.total = static_cast<int>(job->hoardings.size());
        g_lastFences.records = {};
        std::copy_n(job->hoardings.begin(), std::min(job->hoardings.size(), g_lastFences.records.size()), g_lastFences.records.begin());
    }
}

} // namespace

namespace runtime {

// ---- The lifecycle -----------------------------------------------------------------------------------------------------------------------

Manager* Create()
{
    g_answered.Count();
    return new Manager();
}

void Destroy(Manager* manager)
{
    g_answered.Count();
    delete manager;
}

bool LoadJson(Manager* manager, const nlohmann::json& in, std::string& error)
{
    JsonReader reader("ConstructionManager", nullptr, in, error);
    if (reader.Ok()) construction::LoadFields(*manager, reader);
    RecordLoadedFences(*manager);
    return reader.Ok();
}

const LoadedFences& LastLoadedFences()
{
    return g_lastFences;
}

// ---- Group A: the read-only queries -----------------------------------------------------------------------------------------------------------

Job* FindJob(Manager* manager, int buildingId)
{
    g_answeredQueries.Count();
    return construction::FindJob(*manager, buildingId);
}
int CountJobs(Manager* manager, int mode)
{
    g_answeredQueries.Count();
    return construction::CountJobs(*manager, mode);
}
int CountUsableBuilders(Manager* manager)
{
    QueryEnv env;
    g_answeredQueries.Count();
    return construction::CountUsableBuilders(*manager, env);
}
float BuildProgress(Manager* manager, int buildingId)
{
    g_answeredQueries.Count();
    return construction::BuildProgress(*manager, buildingId);
}
float DemolishProgress(Manager* manager, int buildingId)
{
    g_answeredQueries.Count();
    return construction::DemolishProgress(*manager, buildingId);
}
float IsBuilding(Manager* manager, int buildingId)
{
    QueryEnv env;
    g_answeredQueries.Count();
    return construction::IsBuilding(*manager, env, buildingId);
}
float IsDemolishing(Manager* manager, int buildingId)
{
    QueryEnv env;
    g_answeredQueries.Count();
    return construction::IsDemolishing(*manager, env, buildingId);
}
uint8_t IsStarted(Manager* manager, int buildingId)
{
    g_answeredQueries.Count();
    return construction::IsStarted(*manager, buildingId);
}
int GetBuilderLocation(Manager* manager, int builderId)
{
    g_answeredQueries.Count();
    return construction::GetBuilderLocation(*manager, builderId);
}
int GetWorkDirection(Manager* manager, int buildingId, int builderId)
{
    QueryEnv env;
    g_answeredQueries.Count();
    return construction::GetWorkDirection(*manager, env, buildingId, builderId);
}

// ---- Group B: the setters ---------------------------------------------------------------------------------------------------------------------

void SetStarted(Manager* manager, int buildingId, uint8_t started)
{
    g_answered.Count();
    construction::SetStarted(*manager, buildingId, started);
}

void SetBuilderLocation(Manager* manager, int builderId, int location)
{
    g_answered.Count();
    construction::SetBuilderLocation(*manager, builderId, location);
}

// construction::TickHoarding: forwards to SpawnHoarding, nothing else.
void TickHoarding(Manager* manager, int buildingId)
{
    g_answered.Count();
    SpawnHoarding(manager, buildingId);
}

// ---- Group C1 and C2: the jobs and the builder table ------------------------------------------------------------------------------------

// construction::DeleteJob: FinishBuild/FinishDemolish drop the finished job.
void DeleteJob(Manager* manager, int buildingId)
{
    g_answered.Count();
    construction::DeleteJob(*manager, buildingId);
}

// construction::AddBuilder: a builder entering state 1 registers a new builder.
void AddBuilder(Manager* manager, int builderId)
{
    g_callsAddBuilder.Count();
    g_answered.Count();
    BuilderEnv env{manager};
    construction::AddBuilder(*manager, builderId, env);
}

// construction::RemoveBuilder: DestructBuilder unregisters the builder.
void RemoveBuilder(Manager* manager, int builderId)
{
    g_callsRemoveBuilder.Count();
    g_answered.Count();
    BuilderEnv env{manager};
    construction::RemoveBuilder(*manager, builderId, env);
}

} // namespace runtime

} // namespace obl::construction
