#include "game/map/grid_state.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"
#include "game/feedback/feedback_manager.h"
#include "game/world/world_queries_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/map_view_math.h"
#include "core/float_to_int.h"
#include "game/objects/guest_actions.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/ui/text_table.h"

#include <cstdint>

// DecideSwimAction, UpdateDrowning, StartStomachIllness and the death notice SendOutDeathMessage, guest swimming, drowning and illness, and guest walking (docs/architecture/objects.md, Guest). All of them send messages that are delivered
// at once (the guest's own script, or a lifeguard's).
namespace obl::object {
using feedback::Feedback;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::Rand;

namespace {

using guest_actions::NearestLifeguard;
using object::Building;
using object::Guest;

test::CallCounter g_swim("objects.guest-decide-swim-action");
test::CallCounter g_drowning("objects.guest-update-drowning");
test::CallCounter g_badGuts("objects.guest-start-bad-guts");
test::CallCounter g_death("objects.guest-send-out-death-message");

// The fields past data are zero; nobody reads them.
void Post(uint32_t sender, int32_t receiver, int32_t type, int32_t flags, int32_t data)
{
    messages::Message message{};
    message.sender = static_cast<int>(sender);
    message.receiver = receiver;
    message.type = type;
    message.flags = flags;
    message.data = data;
    messages::message_facade::Post(messages::message_facade::Current(), &message);
}

// ---- DecideSwimAction ----------------------------------------------------------------------------------------------------------

void DecideSwim(Guest& guest)
{
    map::Grid* grid = GetGrid();
    const auto target = guest_actions::DecideSwimTarget(
            guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_definition->m_SwimDist, [] { return Rand(); },
            [&](float x, float y) {
                const grid_math::TileXY tile = grid->WorldPointToTile(x, y);
                return grid->Cell(tile.x, tile.y).m_flags;
            });
    // MT_SWIM2SEAPOINT: the target in flags (x) and data (y), truncated toward zero.
    Post(guest.m_id, static_cast<int32_t>(guest.m_id), messages::MT_SWIM2SEAPOINT, FloatToIntWrap(target.x), FloatToIntWrap(target.y));
}

// ---- UpdateDrowning ------------------------------------------------------------------------------------------------------------

// ---- SendOutDeathMessage -------------------------------------------------------------------------------------------------------

// The objects around are the object manager's shared buffer, indexed live (a delivered message may fill it again).
void RunSendOutDeathMessage(Guest& guest)
{
    int tile[2] = {};
    ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
    const auto list = object_manager::GetObjectsNearTile(GetObjectManager(), tile[0], tile[1], guest_actions::kDeathRadius);
    const auto count = static_cast<uint32_t>(list.size());
    const guest_actions::DeathNotice notice = guest_actions::DeathNoticeFor(guest.m_state);
    layout::World* world = GetWorld();
    ++(notice.eaten ? world->m_guestsEaten : world->m_guestsDrowned);
    feedback::Feedback(world->m_feedbackManager)
            .AddText(ui::text_table::Lookup(notice.key), static_cast<int32_t>(guest.m_id), guest_actions::kDeathTickerType);
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i]->m_type != object::ObjectType::Guest) continue;
        Post(guest.m_id, static_cast<int32_t>(list[i]->m_id), messages::MT_SPOT_OPINION, 0, notice.spotKind);
    }
}

void RunUpdateDrowning(Guest& guest)
{
    --guest.m_oxygen;
    const auto guards = object_manager::GetListByClass(GetObjectManager(), static_cast<int>(object::ObjectType::Lifeguard));
    const auto count = static_cast<uint32_t>(guards.size());
    NearestLifeguard nearest;
    for (uint32_t i = 0; i < count; ++i) {
        // Every lifeguard hears of the death before it is measured; the list and the oxygen are reread after each delivery.
        if (guest.m_oxygen == 0) Post(guest.m_id, static_cast<int32_t>(guards[i]->m_id), messages::MT_IMDEAD, 0, 0);
        object::Base* guard = guards[i];
        nearest.Offer(static_cast<int32_t>(guard->m_id), guest_actions::LifeguardDistance(guard->m_WorldXYZ, guest.m_WorldXYZ));
    }
    if (guest.m_oxygen == 0) {
        guest.m_waitingForDeath = 1;
        g_death.Count();
        RunSendOutDeathMessage(guest);
        if (guest.m_StayingAt != -1)
            --static_cast<Building*>(object_manager::ObjectById(GetObjectManager(), guest.m_StayingAt))->m_NumGuests;
    }
    if (nearest.id == NearestLifeguard::kNone || guest.m_waitingForDeath != 0) return;
    const int32_t sea = GetGrid()->GetSeaAreaOfPoint(guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]);
    Post(guest.m_id, nearest.id, messages::MT_DROWNING, 0, sea);
}

// ---- StartStomachIllness --------------------------------------------------------------------------------------------------------------

bool RunStartBadGuts(Guest& guest)
{
    if (!guest.m_imIll) return false;
    float point[3];
    layout::World::RandomWalkPoint(point);
    guest.m_walkpoints[0][0] = point[0];
    guest.m_walkpoints[0][1] = point[1];
    Post(guest.m_id, static_cast<int32_t>(guest.m_id), messages::MT_STATECHANGE, 0, guest_actions::kStateBadGuts);
    // After the immediate state change (its cascade draws first).
    GuestThought(guest, "GUEST_THOUGHT_NOT_WELL"); // the key has no separator before the digit
    return true;
}

} // namespace

void Guest::DecideSwimAction()
{
    g_swim.Count();
    DecideSwim(*this);
}

void Guest::UpdateDrowning()
{
    g_drowning.Count();
    RunUpdateDrowning(*this);
}

void Guest::SendOutDeathMessage()
{
    g_death.Count();
    RunSendOutDeathMessage(*this);
}

bool Guest::StartStomachIllness()
{
    g_badGuts.Count();
    return RunStartBadGuts(*this) ? 1u : 0u;
}

} // namespace obl::object
