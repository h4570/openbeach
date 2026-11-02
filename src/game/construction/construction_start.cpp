#include "game/objects/object_base.h"
#include "game/construction/construction_manager.h"

#include "game/tables/building_behavior.h"
#include "game/construction/construction_job_start.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/messages/message_handler.h"
#include "game/repair/repair_manager.h"
#include "game/world/world_messages.h"

#include <cstdint>
#include "game/ui/map_view_access.h"
#include "game/ui/map_view.h"

namespace obl::construction {
namespace {

using object::Building;
const auto kRemoveRepairJob = &repair::repair_facade::RemoveJob;
const auto kRemoveCleaningJob = &cleaning::cleaning_facade::RemoveJob;
const auto kTilePosition = [](object::Base* object, int* out) { return object->GetTilePosition(out); };

// The game side of the Env of src/game/construction/construction_job_start.h.
struct StartEnv {
    construction::Manager* manager;

    bool Startup() const { return game::GetWorld()->m_startup != 0; }
    bool Building(int id, construction::StartFacts* facts) const
    {
        auto* building = static_cast<object::Building*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
        if (!building) return false;
        const tables::BuildingDef* def = building->m_definition;
        facts->hasDefinition = def != nullptr;
        if (def) {
            facts->buildTime = def->m_BuildTime;
            facts->demolishTime = def->m_DemolishTime;
            facts->hasFootprint = def->m_footprint != nullptr;
            facts->jetty = def->m_Behavior == BuildingBehavior::Jetty;
        }
        return true;
    }
    void ApplyBuildingToGrid(int id) const { runtime::ApplyBuildingToGrid(manager, id); }
    void CollectWorkPoints(int id) const { runtime::CollectWorkPoints(manager, id); }
    void Reallocate() const { runtime::ReallocateBuilders(manager); }
    // Words 5..7 of the messages are left uninitialised (stack garbage); the receivers' scripts do not read them.
    static messages::Message Make(int sender, int receiver, int type, int data)
    {
        messages::Message message{};
        message.sender = sender;
        message.receiver = receiver;
        message.type = type;
        message.flags = 0;
        message.data = data;
        return message;
    }
    void Post(int sender, int receiver, int type, int data) const
    {
        messages::Message message = Make(sender, receiver, type, data);
        messages::message_facade::Post(messages::message_facade::Current(), &message);
    }
    void PostToHumans(int sender, int type, int data) const
    {
        messages::Message message = Make(sender, -1, type, data);
        world::PostMessageToHumans(*game::GetWorld(), message);
    }
    void RemoveRepairJob(int id) const { kRemoveRepairJob(game::GetWorld()->m_repairManager, id); }
    void RemoveCleaningJob(int id) const { kRemoveCleaningJob(game::GetWorld()->m_cleaningManager, id); }
    bool TilePosition(int id, int out[2]) const
    {
        object::Base* building = object::object_manager::ObjectById(game::GetObjectManager(), id);
        if (!building) return false;
        kTilePosition(building, out);
        return true;
    }
    void ApplyFootprint(uint32_t x, uint32_t y, int phase, int mode, int id) const
    {
        runtime::ApplyFootprint(manager, x, y, phase, mode, id);
    }
    bool Extents(int id, int out[4]) const
    {
        auto* building = static_cast<object::Building*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
        if (!building) return false;
        out[0] = building->m_rScrExtents.left;
        out[1] = building->m_rScrExtents.top;
        out[2] = building->m_rScrExtents.right;
        out[3] = building->m_rScrExtents.bottom;
        return true;
    }
    void QueueBlockRebuild(int x, int y, int width, int height) const { ui::GetMapView()->QueueBlockRebuild(x, y, width, height); }
};

} // namespace

namespace runtime {

// construction::StartBuild and construction::StartDemolish: one argument
// (the building id); the building's state entry is the only caller.
void StartBuild(construction::Manager* manager, int id)
{
    StartEnv env{manager};
    construction::StartBuild(*manager, id, env);
}
void StartDemolish(construction::Manager* manager, int id)
{
    StartEnv env{manager};
    construction::StartDemolish(*manager, id, env);
}

} // namespace runtime

} // namespace obl::construction
