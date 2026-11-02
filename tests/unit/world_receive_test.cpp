#include "object_storage.h"
#include "unit_test.h"
#include "game/objects/object_base.h"
#include "game/messages/message_state.h"
#include "game/world/world_state.h"

#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "game/world/world_receive_logic.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_receive;
using obl::messages::Message;

namespace {

struct Slab {
    std::unique_ptr<layout::World> world = std::make_unique<layout::World>();
    std::unique_ptr<world_script::WorldScript> script = std::make_unique<world_script::WorldScript>();
    object::Base object{};

    Slab()
    {
        obl::unit::FillObject<object::Base>(object);
        world->m_worldScript = script.get();
    }
};

struct ReceiveEnv {
    std::vector<std::string> calls;
    object::Base* object = nullptr; // what GetObject finds
    bool createResult = true;

    bool CreateObject(const Message& message, const void*)
    {
        calls.push_back("create " + std::to_string(message.data));
        return createResult;
    }
    object::Base* GetObject(uint32_t id)
    {
        calls.push_back("get " + std::to_string(id));
        return object;
    }
    void RunOnExitList(world_script::WorldScript&) { calls.push_back("exit"); }
    void EnterState(world_script::WorldScript&, uint32_t state) { calls.push_back("enter " + std::to_string(state)); }
    void DispatchMessage(world_script::WorldScript&, const Message& message)
    {
        calls.push_back("dispatch " + std::to_string(message.type));
    }
};

Message Make(int type, int data)
{
    Message message{};
    message.type = type;
    message.data = data;
    return message;
}

struct PlaceEnv {
    std::vector<std::string> calls;
    float timeSeenByDispatch = -1;
    const void* name = nullptr;
    void Dispatch(Message& command, const void* n)
    {
        calls.push_back("dispatch");
        timeSeenByDispatch = command.time;
        name = n;
    }
};

} // namespace

OBL_TEST_CASE("world_receive.a_name_means_a_creation_request_whatever_the_type")
{
    Slab slab;
    ReceiveEnv env;
    const char name[] = "x";
    for (const bool result : {true, false}) {
        env.calls.clear();
        env.createResult = result;
        OBL_CHECK_EQ(ReceiveMessage(*slab.world, Make(kStateChange, 9), name, env), result);
        OBL_CHECK(env.calls == std::vector<std::string>{"create 9"});
        OBL_CHECK_EQ(slab.world->m_scriptState, 0);
    }
}

OBL_TEST_CASE("world_receive.kill_marks_the_object_and_answers_true")
{
    Slab slab;
    ReceiveEnv env;
    env.object = &slab.object;
    OBL_CHECK(ReceiveMessage(*slab.world, Make(kKillObject, 42), nullptr, env));
    OBL_CHECK(env.calls == std::vector<std::string>{"get 42"});
    OBL_CHECK_EQ(slab.object.m_waitingForDeath, 1);
    env.object = nullptr;
    OBL_CHECK(ReceiveMessage(*slab.world, Make(kKillObject, 43), nullptr, env));
}

OBL_TEST_CASE("world_receive.created_is_acknowledged_without_a_call")
{
    Slab slab;
    ReceiveEnv env;
    OBL_CHECK(ReceiveMessage(*slab.world, Make(kObjectCreated, 5), nullptr, env));
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("world_receive.state_change_leaves_remembers_and_enters_in_that_order")
{
    Slab slab;
    ReceiveEnv env;
    OBL_CHECK(ReceiveMessage(*slab.world, Make(kStateChange, 7), nullptr, env));
    OBL_CHECK(env.calls == (std::vector<std::string>{"exit", "enter 7"}));
    OBL_CHECK_EQ(slab.world->m_scriptState, 7);
}

OBL_TEST_CASE("world_receive.state_change_without_a_script_still_remembers_the_state")
{
    Slab slab;
    slab.world->m_worldScript = nullptr;
    ReceiveEnv env;
    OBL_CHECK(ReceiveMessage(*slab.world, Make(kStateChange, 4), nullptr, env));
    OBL_CHECK(env.calls.empty());
    OBL_CHECK_EQ(slab.world->m_scriptState, 4);
}

OBL_TEST_CASE("world_receive.any_other_type_goes_to_the_script_and_answers_true_even_without_one")
{
    Slab slab;
    ReceiveEnv env;
    for (const int type : {2, 4, 0x53, 107, -1}) {
        env.calls.clear();
        OBL_CHECK(ReceiveMessage(*slab.world, Make(type, 0), nullptr, env));
        OBL_CHECK(env.calls == std::vector<std::string>{"dispatch " + std::to_string(type)});
    }
    slab.world->m_worldScript = nullptr;
    env.calls.clear();
    OBL_CHECK(ReceiveMessage(*slab.world, Make(2, 0), nullptr, env));
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("world_receive.place_command_is_stamped_with_the_time_before_the_dispatch")
{
    PlaceEnv env;
    Message command = Make(1, 2);
    command.time = 9.0f;
    const char name[] = "n";
    PostPlaceCommand(12.25f, command, name, env);
    OBL_CHECK(env.calls == std::vector<std::string>{"dispatch"});
    OBL_CHECK_EQ(command.time, 12.25f);
    OBL_CHECK_EQ(env.timeSeenByDispatch, 12.25f);
    OBL_CHECK(env.name == name);
}
