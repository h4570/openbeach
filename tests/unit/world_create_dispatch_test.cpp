#include "unit_test.h"

#include "game/objects/world_create_dispatch.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_create_params.h"

#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_create;
using object::ObjectType;

namespace {

struct FakeEnv {
    std::vector<std::string> calls;
    object::Base object{};
    obl::messages::Message posted{};
    uint8_t paused = 0;
    bool factoryFails = false;
    uint8_t pausedInFactory = 0xff;

    FakeEnv() { object.m_id = 77; }

    uint8_t& Paused() { return paused; }
    object::Base* CreateObject(ObjectType type, const void*)
    {
        calls.push_back("factory " + std::to_string(static_cast<int>(type)));
        pausedInFactory = paused;
        return factoryFails ? nullptr : &object;
    }
    object::Base* CreateObjectByKey(const char* key, ObjectType type)
    {
        calls.push_back("create " + std::string(key) + " " + std::to_string(static_cast<int>(type)));
        pausedInFactory = paused;
        return &object;
    }
    void SetType(object::Base*, ObjectType type) { calls.push_back("type " + std::to_string(static_cast<int>(type))); }
    void SetState(object::Base*, int state) { calls.push_back("state " + std::to_string(state)); }
    void Teleport(object::Base*, float x, float y, float z)
    {
        calls.push_back("teleport " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z));
    }
    void SetHome(object::Base*, int home) { calls.push_back("home " + std::to_string(home)); }
    void PostGlobal(obl::messages::Message& message)
    {
        posted = message;
        calls.push_back("post");
    }
};

obl::messages::Message Request(int kind, int sender = 9)
{
    obl::messages::Message request{};
    request.sender = sender;
    request.data = kind;
    return request;
}

} // namespace

OBL_TEST_CASE("world_create_dispatch.every_class_one_to_twenty_but_nineteen_calls_its_factory")
{
    for (int kind = 1; kind <= 20; ++kind) {
        if (kind == 19) continue;
        FakeEnv env;
        OBL_CHECK(CreateObjectFromMessage(env, Request(kind), nullptr));
        const std::vector<std::string> expected = {"factory " + std::to_string(kind), "post"};
        OBL_CHECK(env.calls == expected);
    }
}

OBL_TEST_CASE("world_create_dispatch.reply_is_object_created_from_the_world_to_the_requester_with_the_new_id")
{
    FakeEnv env;
    OBL_CHECK(CreateObjectFromMessage(env, Request(3, 1234), nullptr));
    OBL_CHECK(env.posted.sender == obl::messages::kMessageReceiverWorld);
    OBL_CHECK(env.posted.receiver == 1234);
    OBL_CHECK(env.posted.type == obl::messages::MT_OBJECTCREATED);
    OBL_CHECK(env.posted.flags == 0);
    OBL_CHECK(env.posted.data == 77);
}

OBL_TEST_CASE("world_create_dispatch.snooty_staff_is_built_inline_without_lookup_or_state_change")
{
    FakeEnv env;
    obl::object::StaffCreateParams record{};
    record.m_key[0] = 'S';
    record.m_pos[0] = 1.5f;
    record.m_pos[1] = -2.0f;
    record.m_pos[2] = 0.25f;
    record.m_homeId = 42;
    OBL_CHECK(CreateObjectFromMessage(env, Request(19), &record));
    const std::vector<std::string> expected = {
            "create S 19", "type 19", "state 0", "teleport 1.500000 -2.000000 0.250000", "home 42", "post"};
    OBL_CHECK(env.calls == expected);
}

OBL_TEST_CASE("world_create_dispatch.unrecognized_classes_do_nothing_and_leave_the_pause_flag_cleared")
{
    for (int kind : {0, 21, -1, 1000}) {
        FakeEnv env;
        env.paused = 1;
        OBL_CHECK(!CreateObjectFromMessage(env, Request(kind), nullptr));
        OBL_CHECK(env.calls.empty());
        OBL_CHECK(env.paused == 0);
    }
}

OBL_TEST_CASE("world_create_dispatch.a_paused_world_is_unpaused_during_the_factory_and_paused_again_after")
{
    FakeEnv env;
    env.paused = 1;
    OBL_CHECK(CreateObjectFromMessage(env, Request(5), nullptr));
    OBL_CHECK(env.pausedInFactory == 0);
    OBL_CHECK(env.paused == 1);
    FakeEnv running;
    OBL_CHECK(CreateObjectFromMessage(running, Request(5), nullptr));
    OBL_CHECK(running.paused == 0);
}

OBL_TEST_CASE("world_create_dispatch.a_factory_without_an_object_posts_nothing_but_repauses")
{
    FakeEnv env;
    env.paused = 1;
    env.factoryFails = true;
    OBL_CHECK(!CreateObjectFromMessage(env, Request(1), nullptr));
    OBL_CHECK(env.paused == 1);
    const std::vector<std::string> expected = {"factory 1"};
    OBL_CHECK(env.calls == expected);
}
