#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/objects/staff_actions.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/entity_accessors.h"
#include "game/objects/staff_actions_logic.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/repair/repair_manager.h"
#include "game/objects/script_program_run.h"
#include "game/objects/script_text.h"

#include <bit>
#include <cstring>
#include <string>
#include <vector>

namespace obl::object {
using game::GetObjectManager;
using game::GetWorld;

namespace {
using namespace world_data;
using messages::Message;
using object::Builder; // object_staff.h
using object::Cleaner;
using object::Lifeguard;
using object::Mechanic;
using object::Rep;
using obl::script::ScriptProgram;
using ui::SpriteInstance;
namespace actions = staff::actions;
using actions::Profession;

test::CallCounter g_counter("objects.staff-actions");

const auto kRepairType = &repair::repair_facade::GetJobType;
const auto kReturnRepair = &repair::repair_facade::ReturnJob;

struct Env {
    object::Staff& employee;
    void SetState(int32_t state) { entity::accessors::SetState(employee, static_cast<uint32_t>(state)); }
    void EnterState(int32_t state) { employee.OnEnterState(static_cast<uint32_t>(state)); }
    int32_t RepairType(int32_t id) { return kRepairType(GetWorld()->m_repairManager, id); }
    void ReturnRepair(int32_t id, int32_t type) { kReturnRepair(GetWorld()->m_repairManager, id, static_cast<uint32_t>(type)); }
    object::Base* Object(int32_t id) { return object_manager::ObjectById(GetObjectManager(), id); }
    void Post(const Message& message)
    {
        Message copy = message;
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    void PostDelayed(const Message& message, float delay)
    {
        Message copy = message;
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &copy, delay);
    }
    void Send(const Message& message) { employee.Send(message); }
    void AnimType(int32_t type) { static_cast<SpriteInstance*>(employee.m_sprite)->m_animType = type; }
    float SetAnimation(const char* name)
    {
        return (static_cast<SpriteInstance*>(employee.m_sprite))->SetAnimByName(name, GetWorld()->m_gameTime, employee.m_WorldXYZ);
    }
    const char* MessageAnimation() { return script::text::Of(static_cast<Message*>(employee.m_currentMessage)->data); }
    void ScriptSend(script::ScriptProgram* script, Message message) { ScriptProgramSendMessage(*script, &employee, message); }
    void ScriptSendDelayed(script::ScriptProgram* script, Message message, float delay)
    {
        ScriptProgramSendMessageDelayed(*script, &employee, message, delay);
    }
};

template<Profession P, class T> float Play(T& employee, const char* name, uint32_t reverse, int32_t end, uint32_t loop, const char* finish)
{
    Env env{employee};
    return actions::PlayAnimation<P>(employee, name, (reverse & 0xff) != 0, end, (loop & 0xff) != 0, finish, env);
}
template<Profession P, class T>
float RunPlay(object::Staff* employee, const char* name, uint32_t reverse, int32_t end, uint32_t loop, const char* finish)
{
    g_counter.Count();
    return Play<P>(static_cast<T&>(*employee), name, reverse, end, loop, finish);
}
void RunSend(object::Staff* employee, Message message)
{
    g_counter.Count();
    Env env{*employee};
    actions::SendMessage(*employee, message, env);
}
void RunDelayed(object::Staff* employee, Message message, float delay)
{
    g_counter.Count();
    Env env{*employee};
    actions::SendMessageDelayed(*employee, message, delay, env);
}

} // namespace

void LifeguardLeave(object::Lifeguard& employee)
{
    g_counter.Count();
    Env env{employee};
    actions::LifeguardLeave(employee, env);
}

void MechanicReturnJobAndLeave(object::Mechanic& employee, bool found)
{
    g_counter.Count();
    Env env{employee};
    actions::ReturnJobAndLeave(employee, found, env);
}

void RepReleaseGuestAndLeave(object::Rep& employee, const uint32_t* guestId, bool sent)
{
    g_counter.Count();
    Env env{employee};
    actions::ReleaseGuestAndLeave(employee, guestId, sent, env);
}

} // namespace obl::object

namespace obl::object {
void Builder::Send(messages::Message message)
{
    object::RunSend(this, message);
}
void Builder::SendDelayed(messages::Message message, float delay)
{
    object::RunDelayed(this, message, delay);
}
float Builder::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return object::RunPlay<object::Profession::Builder, object::Builder>(this, name, reverse, exitState, loop, finish);
}
void Cleaner::Send(messages::Message message)
{
    object::RunSend(this, message);
}
void Cleaner::SendDelayed(messages::Message message, float delay)
{
    object::RunDelayed(this, message, delay);
}
float Cleaner::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return object::RunPlay<object::Profession::Cleaner, object::Cleaner>(this, name, reverse, exitState, loop, finish);
}
void Lifeguard::Send(messages::Message message)
{
    object::RunSend(this, message);
}
void Lifeguard::SendDelayed(messages::Message message, float delay)
{
    object::RunDelayed(this, message, delay);
}
float Lifeguard::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return object::RunPlay<object::Profession::Lifeguard, object::Lifeguard>(this, name, reverse, exitState, loop, finish);
}
void Mechanic::Send(messages::Message message)
{
    object::RunSend(this, message);
}
void Mechanic::SendDelayed(messages::Message message, float delay)
{
    object::RunDelayed(this, message, delay);
}
float Mechanic::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return object::RunPlay<object::Profession::Builder, object::Builder>(this, name, reverse, exitState, loop, finish);
}
void Rep::Send(messages::Message message)
{
    object::RunSend(this, message);
}
void Rep::SendDelayed(messages::Message message, float delay)
{
    object::RunDelayed(this, message, delay);
}
float Rep::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return object::RunPlay<object::Profession::Rep, object::Rep>(this, name, reverse, exitState, loop, finish);
}
void SecurityGuard::Send(messages::Message message)
{
    object::RunSend(this, message);
}
void SecurityGuard::SendDelayed(messages::Message message, float delay)
{
    object::RunDelayed(this, message, delay);
}
float SecurityGuard::PlayAnimation(const char* name, uint32_t reverse, int exitState, uint32_t loop, const char* finish)
{
    return object::RunPlay<object::Profession::Cleaner, object::Cleaner>(this, name, reverse, exitState, loop, finish);
}
} // namespace obl::object
