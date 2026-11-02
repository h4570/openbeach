#include "game/ui/sprite_bank.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/objects/staff_snooty.h"

#include "game/game/game_accessors.h"
#include "game/messages/message_state.h"
#include "core/log.h"
#include "game/objects/staff_snooty_logic.h"
#include "game/objects/script_text.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"

#include <cstdint>
#include <cstring>

namespace obl::object {
using game::GetWorld;
using game::GetWorldParams;

namespace {
using namespace layout;
using messages::Message;
using ui::SpriteInstance;
using enum messages::MessageType;
using object::SnootyStaff;
using namespace tables;
namespace snooty = staff::snooty;

// Calls of the snooty staff's methods (`call-report objects.snooty-staff`); the driver below runs through this counter.
void Drive(int rounds);
test::CallCounter g_counter("objects.snooty-staff", &Drive);

struct GameEnv {
    SnootyStaff& employee;
    void* CreateSprite(const char* name) { return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name); }
    void Stand(void* sprite) { (static_cast<SpriteInstance*>(sprite))->SetAnimByName("d_stand_snooty", GetWorld()->m_gameTime, nullptr); }
    double PlayAnim(int32_t nameWord)
    {
        return (static_cast<SpriteInstance*>(employee.m_sprite))
                ->SetAnimByName(script::text::Of(nameWord), GetWorld()->m_gameTime, employee.m_WorldXYZ);
    }
    void Post(const Message& message)
    {
        Message copy = message; // Post writes the delivery time into the record
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    void PostDelayed(const Message& message, float delay)
    {
        Message copy = message;
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &copy, delay);
    }
    uint8_t Movement(const Message& message) { return employee.Person::ReceiveMessage(const_cast<Message*>(&message)) ? 1 : 0; }
};

uint8_t RunReceive(SnootyStaff* employee, Message* message)
{
    g_counter.Count();
    GameEnv env{*employee};
    return snooty::ReceiveMessage(*employee, *message, env);
}

char RunEnterState(SnootyStaff* employee, uint32_t state)
{
    g_counter.Count();
    return snooty::OnEnterState(state);
}

void RunSetHome(SnootyStaff* employee, int32_t homeId)
{
    g_counter.Count();
    GameEnv env{*employee};
    snooty::SetHome(*employee, homeId, env);
}

// The diagnostic driver creates snooty staff for every definition because normal gameplay does not create them. An unavailable
// sprite name uses the bank's dummy sprite. Exercise the constructor, the message branches (animation names the sprite has and has
// not, with and without the "no answer" flag), OnEnterState, SetHome and the destructor. Their messages go to a home id
// nothing has, so nothing is delivered to a real object.
void Drive(int rounds)
{
    WorldParams* params = GetWorldParams();
    static const char* const kAnims[] = {"d_stand_snooty", "stand", "no_such_animation"};
    for (int round = 0; round < rounds; ++round)
        for (int i = 0; i < params->m_numStaff; ++i) {
            auto* object = new SnootyStaff(params->m_staff[i].m_key.c_str());
            object->m_id = 0x7f000000u + static_cast<uint32_t>(i);
            RunSetHome(object, 0x7f100000 + i);
            for (const char* anim : kAnims)
                for (int32_t flags : {0, 1}) {
                    Message message{};
                    message.sender = 0x7f100000 + i;
                    message.receiver = static_cast<int32_t>(object->m_id);
                    message.type = MT_PLAYANIMATION;
                    message.flags = flags;
                    message.data = script::text::Id(anim);
                    RunReceive(object, &message);
                }
            Message here{};
            here.sender = 0x7f100000 + i;
            here.receiver = static_cast<int32_t>(object->m_id);
            here.type = MT_MOVEIMHERE;
            RunReceive(object, &here);
            if (RunEnterState(object, static_cast<uint32_t>(i)) != 1) OBL_LOG("autotest: FAILED snooty-enter-state answered false");
            delete object;
        }
}
} // namespace

SnootyStaff::SnootyStaff(const char* name) : Staff(name)
{
    g_counter.Count();
    GameEnv env{*this};
    snooty::Construct(*this, name, env);
}
SnootyStaff::~SnootyStaff()
{
    g_counter.Count();
}
bool SnootyStaff::OnEnterState(uint32_t state)
{
    return RunEnterState(this, state);
}
bool SnootyStaff::ReceiveMessage(messages::Message* message)
{
    return RunReceive(this, message);
}
void SnootyStaff::SetHome(int32_t id)
{
    RunSetHome(this, id);
}
} // namespace obl::object
