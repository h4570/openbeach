#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/map/grid_state.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/staff_service.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/map_view_math.h"
#include "core/config.h"
#include "game/objects/staff_roster.h"
#include "game/objects/staff_snooty.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "core/log.h"
#include "game/objects/staff_service_home.h"
#include "game/objects/staff_service_receive.h"
#include "game/objects/staff_service_states.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/game/diagnostics.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace obl::object {
using game::FatalError;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::Rand;

namespace {
using namespace layout;
using messages::Message;
using object::ServiceStaff;
using ui::SpriteInstance;
using namespace tables;
using MessageHandler = messages::Handler;
namespace svc = staff::service;

test::CallCounter g_hasState("objects.service-staff-has-state"), g_enter("objects.service-staff-on-enter-state"),
        g_update("objects.service-staff-update");

MessageHandler* Handler()
{
    return messages::message_facade::Current();
}

struct GameEnv {
    void Post(const Message& message)
    {
        Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(Handler(), &copy);
    }
    void PostDelayed(const Message& message, float delay)
    {
        Message copy = message;
        messages::message_facade::PostDelayed(Handler(), &copy, delay);
    }
};

struct UpdateEnv {
    ServiceStaff& employee;
    bool HumanUpdate() { return employee.Person::Update(); }
    void Fatal(const char* name) { FatalError("Update of staff member \"%s\" failed", name); }
};

char RunHasState(ServiceStaff* employee, uint32_t state)
{
    g_hasState.Count();
    return svc::HasState(state);
}

char RunEnterState(ServiceStaff* employee, uint32_t state)
{
    g_enter.Count();
    GameEnv env;
    return svc::EnterState(*employee, state, env);
}

char RunUpdate(ServiceStaff* employee)
{
    g_update.Count();
    UpdateEnv env{*employee};
    return svc::Update(*employee, env);
}

// ---- ReceiveMessage (group G1b) ----------------------------------------------------------------------------------------------------

test::CallCounter g_receive("objects.service-staff-receive-message");

// Dispatch through Send and SendDelayed so performer subclasses can override message delivery.
struct ReceiveEnv {
    ServiceStaff& employee;
    double PlayAnim(const char* name)
    {
        return (static_cast<SpriteInstance*>(employee.m_sprite))->SetAnimByName(name, GetWorld()->m_gameTime, employee.m_WorldXYZ);
    }
    void Send(const Message& message) { employee.Send(message); }
    void SendDelayed(const Message& message, float delay) { employee.SendDelayed(message, delay); }
    int Rand() { return game::Rand(); }
    float& Cash() { return GetWorld()->m_player->m_cash; }
    float GameTime() { return GetWorld()->m_gameTime; }
    uint8_t Movement(const Message& message) { return employee.Person::ReceiveMessage(const_cast<Message*>(&message)) ? 1 : 0; }
};

uint8_t RunReceive(ServiceStaff* employee, Message* message)
{
    g_receive.Count();
    ReceiveEnv env{*employee};
    return svc::ReceiveMessage(*employee, *message, env);
}

// ---- SetHome (group G1c) -----------------------------------------------------------------------------------------------------------

test::CallCounter g_setHome("objects.service-staff-set-home");

struct HomeEnv {
    ServiceStaff& employee;
    object::Base* Object(int32_t id) { return object_manager::ObjectById(GetObjectManager(), id); }
    BuildingBehavior Behavior(const object::Base& home)
    {
        if (static_cast<uint8_t>(home.m_type) != static_cast<uint8_t>(object::ObjectType::Building)) return BuildingBehavior::None;
        const auto& building = static_cast<const object::Building&>(home);
        return building.m_definition != nullptr ? building.m_definition->m_Behavior : BuildingBehavior::None;
    }
    void ServePosition(const object::Base& home, float* out)
    {
        building_api::GetServePosition(&static_cast<const object::Building&>(home), out);
    }
    void FaceTowards(const float* target, const float* from)
    {
        (static_cast<SpriteInstance*>(employee.m_sprite))->FaceTowards(target[0], target[1], target[2], from[0], from[1], from[2]);
    }
    void FootprintPoint(const object::Base& home, float x, float y, uint32_t rotation, float* out)
    {
        auto* building = const_cast<object::Base*>(&home);
        const BuildingDef* definition = static_cast<const object::Building*>(building)->m_definition;
        int tile[2];
        ui::MapView::WorldToTile(tile, building->m_WorldXYZ);
        const grid_math::PathPoint point =
                GetGrid()->GetFootprintOffsetPoint(tile[0], tile[1], definition->m_GridWidth, definition->m_GridHeight, x, y, rotation);
        out[0] = point.x;
        out[1] = point.y;
    }
    void Teleport(float x, float y, float z) { employee.Teleport(x, y, z); }
    void Send(const Message& message) { employee.Send(message); }
};

void RunSetHome(ServiceStaff* employee, int32_t homeId)
{
    g_setHome.Count();
    HomeEnv env{*employee};
    svc::SetHome(*employee, homeId, env);
}

} // namespace

bool ServiceStaff::HasState(uint32_t state)
{
    return RunHasState(this, state);
}
bool ServiceStaff::OnEnterState(uint32_t state)
{
    return RunEnterState(this, state);
}
bool ServiceStaff::Update()
{
    return RunUpdate(this);
}
bool ServiceStaff::ReceiveMessage(messages::Message* message)
{
    return RunReceive(this, message);
}
void ServiceStaff::SetHome(int32_t id)
{
    RunSetHome(this, id);
}
bool SnootyStaff::Update()
{
    return RunUpdate(reinterpret_cast<ServiceStaff*>(this));
}
} // namespace obl::object
