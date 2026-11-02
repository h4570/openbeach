#include "game/ui/rep_security.h"
#include "game/ui/sprite.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/guest.h"
#include "game/objects/object_guest.h"
#include "game/ui/map_view_math.h"
#include "game/tables/building_behavior.h"
#include "core/fatal.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_building.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "core/log.h"
#include "game/objects/staff_rep_security.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/objects/staff_queries.h"
#include "game/objects/staff_text.h"
#include "game/objects/entity_accessors.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace obl::ui {
namespace {

using object::Guest;
using object::Rep;
using object::SecurityGuard;
using ui::SpriteInstance;
namespace fns = staff::repsec;

enum Which { LookForGuests, ArrivedAtGuest, MoveToNextBuilding, LookForOffender, ChooseAction, Count };
const char* const kNames[Count] = {
        "ui.rep-look-for-guests",
        "ui.rep-arrived-at-guest",
        "ui.rep-move-to-next-building",
        "ui.security-look-for-offender",
        "ui.security-choose-action",
};

// Calls of the helpers (the call-report counters named in the test scripts), and the calls refused because the input cannot be handled.
test::CallCounter g_counter("objects.rep-security-actions"), g_declined("objects.rep-security-actions-declined");
const auto kFindWalkable = &object::staff_facade::FindWalkablePointNear;

// The game side of the Env in src/game/objects/staff_rep_security.h.
template<class Member> struct GameEnv {
    Member& member;
    object::object_manager::ObjectList m_found; // the last list query, read by the pure logic through a pointer
    int Rand() { return game::Rand(); }
    void Post(const messages::Message& message)
    {
        messages::Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    void PostDelayed(const messages::Message& message, float delay)
    {
        messages::Message copy = message;
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &copy, delay);
    }
    object::Base* Object(int32_t id) { return object::object_manager::ObjectById(game::GetObjectManager(), id); }
    uint32_t ObjectsNear(int radius, object::Base* const*& list)
    {
        int32_t tile[2];
        ui::MapView::WorldToTile(tile, member.m_WorldXYZ);
        m_found = object::object_manager::GetObjectsNearTile(game::GetObjectManager(), tile[0], tile[1], radius);
        list = m_found.data();
        return static_cast<uint32_t>(m_found.size());
    }
    uint32_t Buildings(object::Base* const*& list)
    {
        m_found = object::object_manager::GetListByClass(game::GetObjectManager(), static_cast<int>(object::ObjectType::Building));
        list = m_found.data();
        return static_cast<uint32_t>(m_found.size());
    }
    bool IsJetty(const object::Base* building)
    {
        return static_cast<const object::Building*>(building)->m_definition->m_Behavior == BuildingBehavior::Jetty;
    }
    bool IsApproachable(object::Base* guest) { return object::GuestIsApproachable(static_cast<object::Guest&>(*guest)); }
    bool FindWalkablePointNear(float pos[3], int dx, int dy) { return kFindWalkable(&member, pos, dx, dy); }
    void PlayWalk() { member.PlayAnimation("WALK_NORMAL", 0, -1, 1, nullptr); }
    void Position(object::Base* object, float out[3]) { entity::accessors::GetPosition(*object, out); }
    void FaceTowards(SpriteInstance* sprite, const float target[3], const float from[3])
    {
        (sprite)->FaceTowards(target[0], target[1], target[2], from[0], from[1], from[2]);
    }
    void PushThought(uint32_t bits) { object::StaffPushThought(member, bits); }
    uint32_t CountGuests()
    {
        return static_cast<uint32_t>(
                object::object_manager::CountByClass(game::GetObjectManager(), static_cast<char>(object::ObjectType::Guest)));
    }
    uint32_t CountBins() { return static_cast<uint32_t>(object::object_manager::CountByName(game::GetObjectManager(), "bin")); }
};

// false = the input cannot be handled and nothing was drawn, written or sent; Run then ends the game with fatal::UnhandledInput. `answer` is the
// byte result of LookForOffender.
bool TryAnswer(Which which, object::Base& member, uint8_t& answer)
{
    switch (which) {
    case LookForGuests: {
        auto& rep = static_cast<Rep&>(member);
        GameEnv<Rep> env{rep};
        return fns::LookForGuests(rep, env);
    }
    case ArrivedAtGuest: {
        auto& rep = static_cast<Rep&>(member);
        GameEnv<Rep> env{rep};
        return fns::ArrivedAtGuest(rep, env);
    }
    case MoveToNextBuilding: {
        auto& rep = static_cast<Rep&>(member);
        GameEnv<Rep> env{rep};
        return fns::MoveToNextBuilding(rep, env);
    }
    case LookForOffender: {
        auto& guard = static_cast<SecurityGuard&>(member);
        GameEnv<SecurityGuard> env{guard};
        return fns::LookForOffender(guard, env, answer);
    }
    case ChooseAction: {
        auto& guard = static_cast<SecurityGuard&>(member);
        GameEnv<SecurityGuard> env{guard};
        return fns::ChooseAction(guard, env);
    }
    case Count: break;
    }
    return false;
}

// ---- entry points -----------------------------------------------------------------------------------------------------------------------------------

uint8_t Run(Which which, object::Base& member)
{
    g_counter.Count();
    uint8_t answer = 0;
    if (TryAnswer(which, member, answer)) return answer;
    g_declined.Count();
    fatal::UnhandledInput(kNames[which], "the input cannot be handled");
}

} // namespace

void RepLookForGuests(object::Rep& rep)
{
    Run(LookForGuests, rep);
}
void RepArrivedAtGuest(object::Rep& rep)
{
    Run(ArrivedAtGuest, rep);
}
void RepMoveToNextBuilding(object::Rep& rep)
{
    Run(MoveToNextBuilding, rep);
}
uint8_t SecurityLookForOffender(object::SecurityGuard& guard)
{
    return Run(LookForOffender, guard);
}
void SecurityChooseAction(object::SecurityGuard& guard)
{
    Run(ChooseAction, guard);
}

} // namespace obl::ui
