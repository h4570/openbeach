#include "game/objects/object_base.h"
#include "game/construction/construction_job_finish.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/construction/construction_manager.h"
#include "game/construction/placement_types.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/world/world_receive.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace obl::construction {
namespace {

using construction::FinishState;
using object::Building;
const auto kTile = [](object::Base* object, int* out) { return object->GetTilePosition(out); };

enum class Which { Build, Demolish, Spawn };

// The game side of the Env of src/game/construction/construction_job_finish.h.
struct Env {
    construction::Manager* manager;
    Building* building;
    construction::Job* job;
    Which which;

    FinishState Read() const
    {
        const auto* world = game::GetWorld();
        return {world->m_player->m_cash, world->m_incomeToday, world->m_demolishIncomeToday, building->m_Placed,
                job ? static_cast<int>(job->hoardings.size()) : 0, building->m_waitingForDeath};
    }
    void Publish(const FinishState& state)
    {
        if (which == Which::Demolish) {
            auto* world = game::GetWorld();
            world->m_player->m_cash = state.cash;
            world->m_incomeToday = state.income;
            world->m_demolishIncomeToday = state.demolitionIncome;
            building->m_waitingForDeath = state.death;
        } else if (which == Which::Spawn) {
            building->m_Placed = state.placed;
            // A spawn takes the last pending fence; the count never grows.
            if (job && state.hoardings >= 0 && static_cast<size_t>(state.hoardings) < job->hoardings.size())
                job->hoardings.resize(static_cast<size_t>(state.hoardings));
        }
    }
    auto TilePosition() const
    {
        int out[2];
        kTile(building, out);
        return std::array<uint32_t, 2>{static_cast<uint32_t>(out[0]), static_cast<uint32_t>(out[1])};
    }
    int Refund() const { return building->m_definition->m_RefundValue; }
    bool HasFootprint() const { return building->m_definition->m_footprint != nullptr; }
    bool HasJob() const { return job != nullptr; }
    float Time() const { return game::GetWorld()->m_gameTime; }
    construction::Hoarding HoardingAt(int index) const { return job->hoardings[static_cast<size_t>(index)]; }
    uint32_t TileFlags(uint32_t x, uint32_t y) const { return game::GetGrid()->Cell(x, y).m_flags; }
    void ClearFootprint(int id) { runtime::ClearFootprint(manager, id); }
    void ApplyFootprint(uint32_t x, uint32_t y, int phase, int mode, int id) { runtime::ApplyFootprint(manager, x, y, phase, mode, id); }
    void DeleteJob(int id) { runtime::DeleteJob(manager, id); }
    void Reallocate() { runtime::ReallocateBuilders(manager); }
    void PostPlace(const construction::HoardingPlacement& placement)
    {
        construction::HoardingPlacementRecord record{};
        std::memcpy(record.m_name, placement.m_name, sizeof record.m_name);
        std::memcpy(record.m_sprite, placement.m_sprite, sizeof record.m_sprite);
        record.m_x = placement.m_x;
        record.m_y = placement.m_y;
        record.m_usesTilePosition = placement.m_usesTilePosition;
        record.m_direction = placement.m_direction;
        record.m_owner = placement.m_owner;
        messages::Message message{};
        message.sender = message.receiver = -2;
        message.data = 1;
        world::PostPlaceCommand(message, &record);
    }
};

void RunCpp(Which which, int id, FinishState& state, Env& env)
{
    switch (which) {
    case Which::Build: construction::FinishBuild(id, state, env); break;
    case Which::Demolish: construction::FinishDemolish(id, state, env); break;
    case Which::Spawn: construction::SpawnHoarding(id, state, env); break;
    }
}

void Finish(Which which, construction::Manager* manager, int id)
{
    auto* building = static_cast<Building*>(object::object_manager::ObjectById(game::GetObjectManager(), id));
    if (!building || !building->m_definition) return;
    Env env{manager, building, construction::FindJob(*manager, id), which};
    FinishState state = env.Read();

    RunCpp(which, id, state, env);
}

} // namespace

namespace runtime {

// All three entry points take one argument, the building id (the manager comes first).
void FinishBuild(construction::Manager* manager, int id)
{
    Finish(Which::Build, manager, id);
}
void FinishDemolish(construction::Manager* manager, int id)
{
    Finish(Which::Demolish, manager, id);
}
void SpawnHoarding(construction::Manager* manager, int id)
{
    Finish(Which::Spawn, manager, id);
}

} // namespace runtime

} // namespace obl::construction
