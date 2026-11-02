#include "game/objects/staff_direct.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/guest.h"
#include "game/objects/object_guest.h"
#include "core/fatal.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "core/log.h"
#include "game/objects/staff_direct_logic.h"
#include "core/call_counter.h"
#include "game/cleaning/cleaning_manager.h"
#include "game/construction/construction_manager.h"
#include "game/messages/message_handler.h"
#include "game/repair/repair_manager.h"
#include "game/objects/staff_queries.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace obl::object {
using game::GetObjectManager;
using game::GetWorld;

namespace {

using object::Builder;
using object::Cleaner;
using object::Guest;
using object::Lifeguard;
using object::Mechanic;
using object::Rep;
using object::SecurityGuard;
using object::Staff;
namespace fns = staff::direct;

// Every one of the eight takes one argument: a point, a target id or an object id.

enum class Which { Point, Target, Builder, Cleaner, Lifeguard, Mechanic, Rep, Security, Count };
const char* const kNames[static_cast<int>(Which::Count)] = {
        "objects.staff-direct-to-point",
        "objects.staff-direct-to-target",
        "objects.builder-direct-to-object",
        "objects.cleaner-direct-to-object",
        "objects.lifeguard-direct-to-object",
        "objects.mechanic-direct-to-object",
        "objects.rep-direct-to-object",
        "objects.security-guard-direct-to-object",
};
// Calls of the orders (`call-report objects.staff-direct`), and the calls refused because the input cannot be handled (the game then stops).
test::CallCounter g_counter("objects.staff-direct"), g_declined("objects.staff-direct-declined");
const auto kFindWalkable = &object::staff_facade::FindWalkablePointNear;

// The game side of the Env in src/game/objects/staff_direct_logic.h.
template<class Member> struct GameEnv {
    Member& employee;
    void Post(const messages::Message& message)
    {
        messages::Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    void Send(const messages::Message& message) { employee.Send(message); }
    object::Base* Object(int32_t id) { return object_manager::ObjectById(GetObjectManager(), id); }
    bool IsApproachable(object::Base* guest) { return GuestIsApproachable(static_cast<object::Guest&>(*guest)); }
    bool FindWalkablePointNear(float pos[3], int dx, int dy) { return kFindWalkable(&employee, pos, dx, dy); }
    int32_t AssignBuilder(int32_t builderId, uint32_t jobId)
    {
        return construction::runtime::AssignBuilderToJob(GetWorld()->m_constructionManager, builderId, static_cast<int32_t>(jobId));
    }
    // The cleaner's and the mechanic's id are never read by the claim (the facades do not take them).
    int32_t ClaimForCleaner(uint32_t objectId, int32_t /*cleanerId*/)
    {
        return cleaning::cleaning_facade::ClaimObject(GetWorld()->m_cleaningManager, static_cast<int32_t>(objectId));
    }
    int32_t ClaimForMechanic(uint32_t objectId, int32_t /*mechanicId*/, int32_t* type)
    {
        return repair::repair_facade::ClaimObject(
                GetWorld()->m_repairManager, static_cast<int32_t>(objectId), reinterpret_cast<uint32_t*>(type));
    }
};

// false = the input cannot be handled and nothing was written or sent; Run then ends the game with fatal::UnhandledInput.
// `argument` is an object id, except for Point, where it is the point's address.
bool Perform(Which which, object::Base& member, uintptr_t argument)
{
    const auto id = static_cast<uint32_t>(argument);
    const auto target = static_cast<int32_t>(id);
    switch (which) {
    case Which::Point: {
        auto& staff = static_cast<Staff&>(member);
        GameEnv<Staff> env{staff};
        fns::DirectToPoint(staff, reinterpret_cast<const float*>(argument), env);
        return true;
    }
    case Which::Target: {
        auto& staff = static_cast<Staff&>(member);
        GameEnv<Staff> env{staff};
        fns::DirectToTarget(staff, target, env);
        return true;
    }
    case Which::Builder: {
        auto& builder = static_cast<Builder&>(member);
        GameEnv<Builder> env{builder};
        return fns::BuilderDirectToObject(builder, id, env);
    }
    case Which::Cleaner: {
        auto& cleaner = static_cast<Cleaner&>(member);
        GameEnv<Cleaner> env{cleaner};
        return fns::CleanerDirectToObject(cleaner, id, env);
    }
    case Which::Lifeguard: {
        auto& lifeguard = static_cast<Lifeguard&>(member);
        GameEnv<Lifeguard> env{lifeguard};
        return fns::LifeguardDirectToObject(lifeguard, id, env);
    }
    case Which::Mechanic: {
        auto& mechanic = static_cast<Mechanic&>(member);
        GameEnv<Mechanic> env{mechanic};
        return fns::MechanicDirectToObject(mechanic, id, env);
    }
    case Which::Rep: {
        auto& rep = static_cast<Rep&>(member);
        GameEnv<Rep> env{rep};
        return fns::RepDirectToObject(rep, id, env);
    }
    case Which::Security: {
        auto& guard = static_cast<SecurityGuard&>(member);
        GameEnv<SecurityGuard> env{guard};
        return fns::SecurityDirectToObject(guard, id, env);
    }
    case Which::Count: break;
    }
    return false;
}

// ---- handlers -----------------------------------------------------------------------------------------------------------------------------------

template<Which W> void Run(object::Base* employee, uintptr_t argument)
{
    g_counter.Count();
    if (!Perform(W, *employee, argument)) {
        g_declined.Count();
        fatal::UnhandledInput(kNames[static_cast<int>(W)], "the input cannot be handled");
    }
}

} // namespace

void StaffDirectToPoint(object::Staff& member, float* point)
{
    Run<Which::Point>(&member, reinterpret_cast<uintptr_t>(point));
}

void StaffDirectToTarget(object::Staff& member, int32_t target)
{
    Run<Which::Target>(&member, static_cast<uint32_t>(target));
}

} // namespace obl::object

namespace obl::object {
void Builder::DirectToObject(int32_t id)
{
    Run<Which::Builder>(this, id);
}
void Cleaner::DirectToObject(int32_t id)
{
    Run<Which::Cleaner>(this, id);
}
void Lifeguard::DirectToObject(int32_t id)
{
    Run<Which::Lifeguard>(this, id);
}
void Mechanic::DirectToObject(int32_t id)
{
    Run<Which::Mechanic>(this, id);
}
void Rep::DirectToObject(int32_t id)
{
    Run<Which::Rep>(this, id);
}
void SecurityGuard::DirectToObject(int32_t id)
{
    Run<Which::Security>(this, id);
}
} // namespace obl::object
