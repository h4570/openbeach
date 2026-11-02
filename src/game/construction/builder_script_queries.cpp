#include "game/construction/builder_script_queries.h"
#include "game/objects/object_base.h"

#include "game/construction/builder_script_actions.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_registry_api.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_base.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>

namespace obl::construction {
namespace {

using object::Builder;
using object::Person;

// The game side of the Env in src/game/construction/builder_script_actions.h.
struct GameEnv {
    using Object = const void*;
    const Object* GuestList(uint32_t& count)
    {
        m_guests = object::object_manager::GetListByClass(game::GetObjectManager(), static_cast<int>(object::ObjectType::Guest));
        count = static_cast<uint32_t>(m_guests.size());
        return reinterpret_cast<const Object*>(m_guests.data());
    }
    void TilePosition(Object object, int32_t out[2]) { static_cast<const object::Base*>(object)->GetTilePosition(out); }
    bool IsFemale(Object guest) { return static_cast<const Person*>(guest)->m_definition->m_Gender == 1; }
    int32_t& Whistling() { return game::NumBuildersWhistling(); }
    // Words 0..5 are written here, word 7 (the delivery time) is left to Post; floatData is zeroed.
    void Post(int sender, int receiver, int type)
    {
        messages::Message message{};
        message.sender = sender;
        message.receiver = receiver;
        message.type = type;
        message.flags = 0;
        message.data = 0;
        message.extra = construction::kMsgBuilderSource;
        message.floatData = 0.0f;
        messages::message_facade::Post(messages::message_facade::Current(), &message);
    }

    uint32_t Width() { return game::GetGrid()->m_width; }
    uint32_t Height() { return game::GetGrid()->m_height; }
    int TileSize() { return game::GetGrid()->m_tileSize; }
    uint32_t CellFlags(uint32_t x, uint32_t y) { return game::GetGrid()->Cell(x, y).m_flags; }

private:
    object::object_manager::ObjectList m_guests; // the last GuestList result, read through the pointer it returned
};

void CheckFemaleGuests(Builder* builder)
{
    GameEnv env;
    construction::CheckForFemaleGuests(static_cast<const void*>(builder), static_cast<int>(builder->m_id), env);
}

void FindWorkTile(Builder* builder)
{
    GameEnv env;
    int32_t point[2];
    if (!construction::FindNearestValidTile(static_cast<const void*>(builder), env, point)) return;
    builder->m_NearestValidTileX = point[0];
    builder->m_NearestValidTileY = point[1];
}

} // namespace

void CheckForFemaleGuests(Builder* builder)
{
    CheckFemaleGuests(builder);
}

void FindNearestValidTile(Builder* builder)
{
    FindWorkTile(builder);
}

} // namespace obl::construction
