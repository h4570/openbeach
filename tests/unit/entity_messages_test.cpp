#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/entity_messages.h"
#include "game/objects/object_base.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl;
namespace {
using object::Mover;
using obl::messages::Message;

struct Holder {
    alignas(Mover) unsigned char bytes[sizeof(Mover)];
    Holder()
    {
        std::memset(bytes, 0, sizeof(bytes));
        obl::unit::StampVtable<Mover>(bytes);
    }
    Mover& Object() { return *reinterpret_cast<Mover*>(bytes); }
};

// The three virtual calls and the FatalErrors, recorded in order with the state the object had at the time of each call.
struct BaseFakeEnv {
    struct Call {
        std::string name;
        uint32_t argument;
        int stateAtTheTime;
    };
    struct Fatality {
        entity::Fatal kind;
        int sender, data;
    };
    std::vector<Call> calls;
    std::vector<Fatality> fatalities;
    bool hasState = true, exitState = true, enterState = true;

    bool HasState(object::Base& o, uint32_t state)
    {
        calls.push_back({"has", state, o.m_state});
        return hasState;
    }
    bool ExitState(object::Base& o)
    {
        calls.push_back({"exit", 0, o.m_state});
        return exitState;
    }
    bool EnterState(object::Base& o, uint32_t state)
    {
        calls.push_back({"enter", state, o.m_state});
        return enterState;
    }
    void Fatal(entity::Fatal kind, const object::Base&, const Message& m) { fatalities.push_back({kind, m.sender, m.data}); }
};

Message Make(int type, int data = 0, int sender = 0)
{
    Message message{};
    message.type = type;
    message.data = data;
    message.sender = sender;
    return message;
}
} // namespace

OBL_TEST_CASE("base_messages.state_change_exits_the_old_state_then_enters_the_new_one")
{
    Holder h;
    h.Object().m_state = 5;
    BaseFakeEnv env;
    OBL_CHECK(entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_STATECHANGE, 9), env));
    OBL_REQUIRE(env.calls.size() == 3);
    OBL_CHECK_EQ(env.calls[0].name, std::string("has"));
    OBL_CHECK_EQ(env.calls[0].argument, 9u);
    OBL_CHECK_EQ(env.calls[0].stateAtTheTime, 5);
    OBL_CHECK_EQ(env.calls[1].name, std::string("exit"));
    OBL_CHECK_EQ(env.calls[1].stateAtTheTime, 5); // the exit still sees the old state
    OBL_CHECK_EQ(env.calls[2].name, std::string("enter"));
    OBL_CHECK_EQ(env.calls[2].argument, 9u);
    OBL_CHECK_EQ(env.calls[2].stateAtTheTime, 9); // and the entry the new one
    OBL_CHECK_EQ(h.Object().m_state, 9);
    OBL_CHECK(env.fatalities.empty());
}

OBL_TEST_CASE("base_messages.state_change_failures_are_reported_and_the_walk_goes_on")
{
    {
        Holder h;
        BaseFakeEnv env;
        env.hasState = false;
        OBL_CHECK(entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_STATECHANGE, 3, 41), env));
        OBL_REQUIRE(env.fatalities.size() == 1);
        OBL_CHECK(env.fatalities[0].kind == entity::Fatal::UnsupportedState);
        OBL_CHECK_EQ(env.fatalities[0].sender, 41);
        OBL_CHECK_EQ(env.fatalities[0].data, 3);
        OBL_CHECK_EQ(env.calls.size(), 3u);
    }
    {
        Holder h;
        BaseFakeEnv env;
        env.exitState = false;
        entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_STATECHANGE, 3), env);
        OBL_REQUIRE(env.fatalities.size() == 1);
        OBL_CHECK(env.fatalities[0].kind == entity::Fatal::ExitState);
    }
    {
        Holder h;
        BaseFakeEnv env;
        env.enterState = false;
        entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_STATECHANGE, 3), env);
        OBL_REQUIRE(env.fatalities.size() == 1);
        OBL_CHECK(env.fatalities[0].kind == entity::Fatal::EnterState);
        OBL_CHECK_EQ(h.Object().m_state, 3);
    }
}

OBL_TEST_CASE("base_messages.set_owner_stores_a_byte_and_rejects_values_under_six")
{
    Holder h;
    BaseFakeEnv env;
    OBL_CHECK(entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_SETOWNER, 6), env));
    OBL_CHECK_EQ(h.Object().m_owner, 6);
    OBL_CHECK(env.fatalities.empty());
    OBL_CHECK(entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_SETOWNER, 0x1ff), env)); // only the low byte is stored
    OBL_CHECK_EQ(h.Object().m_owner, 0xff);
    OBL_CHECK(env.fatalities.empty());
    OBL_CHECK(entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_SETOWNER, 5), env));
    OBL_REQUIRE(env.fatalities.size() == 1);
    OBL_CHECK(env.fatalities[0].kind == entity::Fatal::InvalidOwner);
    OBL_CHECK_EQ(h.Object().m_owner, 5);
    // a negative value is a huge unsigned number: valid
    env.fatalities.clear();
    entity::ReceiveMessage(h.Object(), Make(obl::messages::MT_SETOWNER, -1), env);
    OBL_CHECK(env.fatalities.empty());
    OBL_CHECK_EQ(h.Object().m_owner, 0xff);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("base_messages.every_other_type_answers_false_and_touches_nothing")
{
    for (int type = -3; type < 120; ++type) {
        if (type == obl::messages::MT_STATECHANGE || type == obl::messages::MT_SETOWNER) continue;
        Holder h;
        h.Object().m_state = 12;
        h.Object().m_owner = 7;
        unsigned char before[sizeof(Mover)];
        std::memcpy(before, h.bytes, sizeof(before));
        BaseFakeEnv env;
        OBL_CHECK(!entity::ReceiveMessage(h.Object(), Make(type, 99), env));
        OBL_CHECK(std::memcmp(before, h.bytes, sizeof(before)) == 0);
        OBL_CHECK(env.calls.empty());
        OBL_CHECK(env.fatalities.empty());
    }
}
