#include "game/construction/construction_manager.h"

#include "game/construction/construction_jobs.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/messages/message_handler.h"

#include <cstdint>

namespace obl::construction {
namespace {

using object::Building;
using tables::BuildingDef;

// The game side of the Env in src/game/construction/construction_jobs.h (group F2).
struct CostPointEnv {
    bool Building(int id, construction::CostFacts* facts) const
    {
        auto* building = static_cast<object::Building*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
        if (!building) return false;
        const BuildingDef* def = building->m_definition;
        facts->hasDefinition = def != nullptr;
        if (def) {
            facts->buildTime = static_cast<uint32_t>(def->m_BuildTime);
            facts->demolishTime = static_cast<uint32_t>(def->m_DemolishTime);
            facts->buildOn = def->m_BuildOn;
        }
        return true;
    }
    float FrameTime() const { return game::GetWorld()->m_gameDeltaTime; }
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
    void PostDelayed(int sender, int receiver, int type, int data, float delay) const
    {
        messages::Message message = Make(sender, receiver, type, data);
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &message, delay);
    }
};

} // namespace

// construction::RemoveCostPoint: BuilderCallFunction is the only caller, with the builder's current job.
void runtime::RemoveCostPoint(construction::Manager* manager, int buildingId)
{
    const CostPointEnv env;
    construction::RemoveCostPoint(*manager, buildingId, env);
}

} // namespace obl::construction
