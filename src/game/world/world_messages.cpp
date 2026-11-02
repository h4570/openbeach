#include "game/world/world_messages.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/messages/message_state.h"
#include "game/world/world_state.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/world/world_post_message.h"
#include "game/messages/message_handler.h"

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace obl::world {
namespace {

using messages::Message;
using MessageHandler = messages::Handler;
using world_data::World;

// The game side of the Env contract in src/game/world/world_post_message.h.
class GameEnv {
public:
    explicit GameEnv(MessageHandler* handler) : m_handler(handler) {}
    world_post::List GetList(int cls)
    {
        m_objects = object::object_manager::GetListByClass(game::GetObjectManager(), cls);
        return {m_objects.data(), static_cast<uint32_t>(m_objects.size())};
    }
    void Post(Message& message) { messages::message_facade::Post(m_handler, &message); }

private:
    MessageHandler* m_handler;
    object::object_manager::ObjectList m_objects; // the list GetList returned, valid until the next call
};

// Call counters (`call-report <name>`); the other counters of each pair only carry `call-drive <name>`.
struct Counters {
    test::CallCounter drive;
    test::CallCounter answered;
    Counters(const char* driveName, const char* answeredName) : drive(driveName), answered(answeredName) {}
};

Counters g_humans("world.post-message-to-humans.drive", "world.post-message-to-humans");
Counters g_classAlt("world.post-message-to-class-alt.drive", "world.post-message-to-class-alt");
Counters g_buildings("world.post-message-to-buildings.drive", "world.post-message-to-buildings");
Counters g_class("world.post-message-to-class.drive", "world.post-message-to-class");

void PostToHumansEntry(World* world, Message* message)
{
    g_humans.answered.Count();
    GameEnv env(world->m_messageHandler);
    world_post::PostToHumans(env, *message);
}

void PostToClassAltEntry(World* world, Message* message, int cls)
{
    g_classAlt.answered.Count();
    GameEnv env(world->m_messageHandler);
    world_post::PostToClass(env, cls, *message);
}

void PostToBuildingsEntry(World* world, Message* message)
{
    g_buildings.answered.Count();
    GameEnv env(world->m_messageHandler);
    world_post::PostToBuildings(env, *message);
}

void PostToClassByHandler(MessageHandler* handler, int cls, Message* message)
{
    g_class.answered.Count();
    GameEnv env(handler);
    world_post::PostToClass(env, cls, *message);
}

// `call-drive world.post-message-to<...> <rounds>`: a message no script answers (MT_DIALOG_CLOSED) to every class the function can reach,
// so the run also covers the classes a short game does not fill (vehicles, tables, shows). It is delivered for real.
Message DriveMessage()
{
    Message message{};
    message.sender = -2;
    message.receiver = -1;
    message.type = messages::MT_DIALOG_CLOSED;
    return message;
}

void DriveHumans(int rounds)
{
    World* world = game::GetWorld();
    for (int i = 0; world && i < rounds; ++i) {
        Message message = DriveMessage();
        PostToHumansEntry(world, &message);
    }
}

void DriveBuildings(int rounds)
{
    World* world = game::GetWorld();
    for (int i = 0; world && i < rounds; ++i) {
        Message message = DriveMessage();
        PostToBuildingsEntry(world, &message);
    }
}

// Every object class of ObjectManager::CreateObject (1-20), including the empty ones and the out-of-range class 0 and 21.
void DriveClassAlt(int rounds)
{
    World* world = game::GetWorld();
    for (int i = 0; world && i < rounds; ++i)
        for (int cls = 0; cls <= 21; ++cls) {
            Message message = DriveMessage();
            PostToClassAltEntry(world, &message, cls);
        }
}

} // namespace

void PostMessageToHumans(World& world, Message& message)
{
    PostToHumansEntry(&world, &message);
}
void PostMessageToWorldClass(World& world, Message& message, int cls)
{
    PostToClassAltEntry(&world, &message, cls);
}
void PostMessageToBuildings(World& world, Message& message)
{
    PostToBuildingsEntry(&world, &message);
}
void PostMessageToClass(MessageHandler* handler, int cls, Message& message)
{
    PostToClassByHandler(handler, cls, &message);
}

bool RegisterWorldMessageDrivers()
{
    g_humans.drive.SetDriver(&DriveHumans);
    g_classAlt.drive.SetDriver(&DriveClassAlt);
    g_buildings.drive.SetDriver(&DriveBuildings);
    return true;
}

} // namespace obl::world
