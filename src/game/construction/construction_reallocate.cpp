#include "game/construction/construction_manager.h"

#include "game/construction/construction_jobs.h"
#include "game/game/game_accessors.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/object_base.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/messages/message_handler.h"
#include "game/objects/object_staff.h"

#include <cstdint>

namespace obl::construction {
namespace {

using object::Builder;
using object::Building;
const auto kIsOpen = &object::building_api::IsOpen;

// The game side of the Env in src/game/construction/construction_jobs.h.
struct ReallocateEnv {
    construction::Manager* manager;
    static object::Base* Object(int id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    bool Builder(int id, construction::BuilderFacts* facts) const
    {
        auto* builder = static_cast<object::Builder*>(Object(id));
        if (!builder) return false;
        auto* home = static_cast<object::Building*>(Object(builder->m_HomeID));
        facts->homeExists = home != nullptr;
        facts->onShift = facts->homeOpen = false;
        if (home) {
            facts->onShift = builder->IsOnShift();
            facts->homeOpen = kIsOpen(home);
        }
        facts->inHut = builder->m_InHut;
        facts->state = builder->m_state;
        return true;
    }
    bool BuilderToken(int id, int* token) const
    {
        auto* builder = static_cast<object::Builder*>(Object(id));
        if (!builder) return false;
        *token = builder->m_BuilderNumber;
        return true;
    }
    bool MaxBuilders(int buildingId, float* limit) const
    {
        auto* building = static_cast<object::Building*>(Object(buildingId));
        if (!building || !building->m_definition) return false;
        *limit = building->m_definition->m_MaxBuilders;
        return true;
    }
    void FreeBuilder(int builderId) const { runtime::ClearBuilderJobEntries(manager, builderId); }
    // Words 5..7 of the message are left uninitialised (stack garbage); the receivers' scripts do not read them.
    void Post(int builderId, int type, int buildingId) const
    {
        messages::Message message{};
        message.sender = message.receiver = builderId;
        message.type = type;
        message.flags = 0;
        message.data = buildingId;
        messages::message_facade::Post(messages::message_facade::Current(), &message);
    }
};

} // namespace

// construction::ReallocateBuilders: AddBuilder/RemoveBuilder, StartBuild, StartDemolish, FinishBuild, FinishDemolish,
// Builder::Update and CallFunction end with it.
void runtime::ReallocateBuilders(construction::Manager* manager)
{
    const ReallocateEnv env{manager};
    construction::ReallocateBuilders(*manager, env);
}

} // namespace obl::construction
