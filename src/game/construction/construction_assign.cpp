#include "game/objects/object_base.h"
#include "game/construction/construction_manager.h"

#include "game/construction/construction_jobs.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/object_base.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <cstdint>
#include "game/ui/map_view_math.h"

namespace obl::construction {
namespace {

using object::Building;
const auto kGetTilePosition = [](object::Base* object, int* out) { return object->GetTilePosition(out); };

test::CallCounter g_callsFree("construction.builder-free-builder-of-job.calls");
test::CallCounter g_answered("construction.assign");

// The game side of the Env in src/game/construction/construction_jobs.h.
struct AssignEnv {
    object::Building* Building(int id) const
    {
        auto* object = static_cast<object::Building*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
        return object && object->m_definition ? object : nullptr;
    }
    void WorldToTile(const float* world, int out[2]) const { ui::MapView::WorldToTile(out, world); }
    bool BuildingTile(int id, int out[2]) const
    {
        object::Building* building = Building(id);
        if (!building) return false;
        kGetTilePosition(building, out);
        return true;
    }
    bool MaxBuilders(int id, float* limit) const
    {
        const object::Building* building = Building(id);
        if (!building) return false;
        *limit = building->m_definition->m_MaxBuilders;
        return true;
    }
    bool BuilderToken(int id, int* token) const
    {
        auto* object = static_cast<object::Builder*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
        if (!object) return false;
        *token = object->m_BuilderNumber;
        return true;
    }
};

} // namespace

namespace runtime {

// construction::FreeBuilderOfJob: gives the job back; the table keeps the builder.
void FreeBuilderOfJob(construction::Manager* manager, int builderId)
{
    g_callsFree.Count();
    g_answered.Count();
    AssignEnv env;
    construction::FreeBuilderOfJob(*manager, builderId, env);
}

// construction::ClearBuilderJobEntries: the same without telling the job (ReallocateBuilders and the script call it).
void ClearBuilderJobEntries(construction::Manager* manager, int builderId)
{
    g_answered.Count();
    construction::ClearBuilderJobEntries(*manager, builderId);
}

// construction::AssignBuilderToNearestJob: builder id, world position (float*, read through
// the map view's WorldToTile), mode, `outIsBuild`. Called by the script's `LookForNearestJob`.
int AssignBuilderToNearestJob(construction::Manager* manager, int builderId, const float* position, int mode, uint32_t* outIsBuild)
{
    g_answered.Count();
    AssignEnv env;
    return construction::AssignBuilderToNearestJob(*manager, builderId, position, mode, outIsBuild, env);
}

// construction::AssignBuilderToJob: builder id, the job's building id. Returns 0/1 or 0xffffffff.
int AssignBuilderToJob(construction::Manager* manager, int builderId, int jobId)
{
    g_answered.Count();
    AssignEnv env;
    return construction::AssignBuilderToJob(*manager, builderId, jobId, env);
}

} // namespace runtime

} // namespace obl::construction
