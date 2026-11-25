#include "game/world/world_receive.h"
#include "game/world/world_script.h"
#include "game/world/world_create.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "core/call_counter.h"
#include "game/world/world_receive_logic.h"
#include "game/messages/message_handler.h"

#include <cstdint>

namespace obl::world {
namespace {

using messages::Message;
using world_data::World;

// Call counters (`call-report <name>`).
test::CallCounter g_receiveCalls("world.receive-message");
test::CallCounter g_placeCalls("world.post-place-command");

// ---- the game side of the Env contracts in src/game/world/world_receive_logic.h --------------------------------------------------------

class GameEnv {
public:
    explicit GameEnv(World& world) : m_world(world) {}

    bool CreateObject(const Message& request, const void* name) { return world::CreateObjectFromMessage(m_world, request, name); }
    object::Base* GetObject(uint32_t id) { return object::object_manager::ObjectById(m_world.m_objectManager, static_cast<int>(id)); }
    void RunOnExitList(world_script::WorldScript& script)
    {
        ScenarioScriptScope scope;
        WorldScriptRunOnExit(script);
    }
    void EnterState(world_script::WorldScript& script, uint32_t state)
    {
        ScenarioScriptScope scope;
        WorldScriptEnterState(script, state);
    }
    void DispatchMessage(world_script::WorldScript& script, const Message& message)
    {
        ScenarioScriptScope scope;
        WorldScriptDispatch(script, message);
    }
    bool ChangeModScriptState(uint32_t modIndex, uint32_t state) { return ChangeModWorldScriptState(m_world, modIndex, state); }
    void DispatchModMessage(const Message& message) { DispatchModWorldMessage(m_world, message); }

private:
    World& m_world;
};

class PlaceEnv {
public:
    void Dispatch(Message& command, const void* name)
    {
        messages::message_facade::Dispatch(*messages::message_facade::Current(), nullptr, command, static_cast<const char*>(name));
    }
};

// ---- entry points ---------------------------------------------------------------------------------------------------------------------

bool ReceiveMessageEntry(World* world, Message* message, const void* name)
{
    g_receiveCalls.Count();
    GameEnv env(*world);
    return world_receive::ReceiveMessage(*world, *message, name, env);
}

} // namespace

bool ReceiveMessage(World& world, Message& message, const void* name)
{
    return ReceiveMessageEntry(&world, &message, name);
}
void PostPlaceCommand(Message& command, const void* name)
{
    g_placeCalls.Count();
    PlaceEnv env;
    world_receive::PostPlaceCommand(game::GetWorld()->m_gameTime, command, name, env);
}

} // namespace obl::world
