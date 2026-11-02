#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/messages/message_state.h"
#include "game/ui/sprite.h"
#include "unit_test.h"

#include "game/objects/staff_service_receive.h"
#include "object_storage.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::messages::Message;
using ui::Sprite;
using ui::SpriteAnim;
using ui::SpriteInstance;
using enum obl::messages::MessageType;
using namespace obl::tables;
namespace svc = obl::staff::service;

namespace {

struct FakeEnv {
    std::string log;
    std::vector<Message> sent;
    std::vector<float> delays; // -1 for a message sent at once
    double length = 1.5;
    int draw = 0;
    float cash = 1000.0f;
    float time = 0.0f;
    uint8_t movement = 0x5a;

    double PlayAnim(const char* name)
    {
        log += std::string(name) + " ";
        return length;
    }
    void Send(const Message& message)
    {
        sent.push_back(message);
        delays.push_back(-1.0f);
    }
    void SendDelayed(const Message& message, float delay)
    {
        sent.push_back(message);
        delays.push_back(delay);
    }
    int Rand()
    {
        log += "rand ";
        return draw;
    }
    float& Cash() { return cash; }
    float GameTime() { return time; }
    uint8_t Movement(const Message&)
    {
        log += "movement ";
        return movement;
    }
};

struct Member {
    obl::object::ServiceStaff object;
    StaffDef definition;
    SpriteInstance instance;
    Sprite sprite;
    SpriteAnim anims[2];
    SpriteAnim* table[2];
    char names[2][16];

    Member(const char* spriteName, int32_t state)
    {
        // The object gets its vtable like a game object (FillObject); the StaffDef's std::string is not memset.
        obl::unit::FillObject(object);
        std::memset(static_cast<void*>(&instance), 0, sizeof(instance));
        std::memset(static_cast<void*>(&sprite), 0, sizeof(sprite));
        std::memset(static_cast<void*>(anims), 0, sizeof(anims));
        std::memset(static_cast<void*>(table), 0, sizeof(table));
        std::memset(static_cast<void*>(names), 0, sizeof(names));
        object.m_id = 77;
        object.m_HomeID = 12;
        object.m_Wage = 30;
        object.m_actionState = state;
        definition.m_Sprite = spriteName;
        object.m_staffDefinition = &definition;
        object.m_sprite = &instance;
        instance.m_sprite = &sprite;
        instance.m_fps = 18;
        std::strcpy(names[0], "stand");
        std::strcpy(names[1], "catch_burger");
        for (int i = 0; i < 2; ++i) {
            anims[i].m_name = names[i];
            table[i] = &anims[i];
        }
        anims[1].m_frames = 27;
        sprite.m_animCount = 2;
        sprite.m_anims = table;
    }
};

Message Told(int type, int data = 0)
{
    Message message{};
    message.sender = 5;
    message.receiver = 77;
    message.type = type;
    message.data = data;
    return message;
}

void CheckSelf(const Message& message, int type, int data)
{
    OBL_CHECK_EQ(message.sender, 77);
    OBL_CHECK_EQ(message.receiver, 77);
    OBL_CHECK_EQ(message.type, type);
    OBL_CHECK_EQ(message.flags, 0);
    OBL_CHECK_EQ(message.data, data);
}

OBL_TEST_CASE("staff_service_receive.counter_follows_the_sprite_of_the_definition")
{
    OBL_CHECK(svc::CounterOf("BARMAN") == svc::Counter::Bar);
    OBL_CHECK(svc::CounterOf("BARWOMAN") == svc::Counter::Bar);
    OBL_CHECK(svc::CounterOf("CHEF") == svc::Counter::Kitchen);
    OBL_CHECK(svc::CounterOf("CHEFFFETTE") == svc::Counter::Kitchen);
    OBL_CHECK(svc::CounterOf("CHEFFETTE") == svc::Counter::None); // the name the sprite really has
    OBL_CHECK(svc::CounterOf("barman") == svc::Counter::Bar);
}

OBL_TEST_CASE("staff_service_receive.serving_a_guest_hands_the_goods_over_after_a_second")
{
    Member member("BARMAN", 0);
    FakeEnv env;
    Message told = Told(MT_SERVE_GUEST, 901);
    told.flags = 3;
    told.floatData = 4.5f;
    OBL_CHECK_EQ(svc::ReceiveMessage(member.object, told, env), svc::LowByte(told));
    OBL_CHECK_EQ(env.log, std::string()); // idle: no "stand"
    OBL_CHECK_EQ(env.sent.size(), size_t(2));
    OBL_CHECK_EQ(env.sent[0].sender, 77);
    OBL_CHECK_EQ(env.sent[0].receiver, 901);
    OBL_CHECK_EQ(env.sent[0].type, int(MT_SERVED_COMMOD));
    OBL_CHECK_EQ(env.sent[0].flags, 3);
    OBL_CHECK_EQ(env.sent[0].data, 901);
    OBL_CHECK_EQ(env.sent[0].floatData, 4.5f);
    OBL_CHECK_EQ(env.delays[0], 1.0f);
    CheckSelf(env.sent[1], MT_PLAYANIMATION, 1);
    OBL_CHECK_EQ(env.delays[1], 1.0f);
    OBL_CHECK_EQ(member.object.m_actionState, 1);

    FakeEnv busy;
    OBL_CHECK_EQ(svc::ReceiveMessage(member.object, told, busy), svc::LowByte(told));
    OBL_CHECK_EQ(busy.log, std::string("stand "));
}

OBL_TEST_CASE("staff_service_receive.the_cook_serves_when_the_burger_is_caught")
{
    Member member("CHEF", 0);
    FakeEnv env;
    const Message told = Told(MT_SERVE_GUEST, 901);
    OBL_CHECK_EQ(svc::ReceiveMessage(member.object, told, env), uint8_t(27));
    OBL_CHECK_EQ(env.delays[0], static_cast<float>(27.0 / 18.0 + 1.0));
    OBL_CHECK_EQ(env.delays[1], 1.0f);
}

OBL_TEST_CASE("staff_service_receive.a_new_day_takes_the_wage_from_the_cash")
{
    Member member("BARMAN", 4);
    FakeEnv env;
    const Message told = Told(MT_NEW_DAY);
    OBL_CHECK_EQ(svc::ReceiveMessage(member.object, told, env), svc::LowByte(told));
    OBL_CHECK_EQ(env.cash, 970.0f);
    OBL_CHECK(env.sent.empty());
    OBL_CHECK_EQ(member.object.m_actionState, 4);
}

OBL_TEST_CASE("staff_service_receive.commodity_quality_tells_home_a_guest_was_served")
{
    Member member("DJ", 0);
    FakeEnv env;
    svc::ReceiveMessage(member.object, Told(MT_COMMOD_QUALITY, 9), env);
    OBL_CHECK_EQ(env.sent.size(), size_t(1));
    OBL_CHECK_EQ(env.delays[0], -1.0f);
    OBL_CHECK_EQ(env.sent[0].sender, 77);
    OBL_CHECK_EQ(env.sent[0].receiver, 12);
    OBL_CHECK_EQ(env.sent[0].type, int(MT_GUEST_SERVED));
    OBL_CHECK_EQ(env.sent[0].data, 0);
}

OBL_TEST_CASE("staff_service_receive.the_dj_walks_its_fourteen_steps_and_holds_the_dance")
{
    Member member("DJ", 0);
    std::string anims;
    for (uint32_t step = 0; step < svc::kDjStepCount; ++step) {
        FakeEnv env;
        env.time = 77.25f;
        env.length = 0.7; // not a float: the held delay is the product rounded once
        const uint8_t answer = svc::ReceiveMessage(member.object, Told(MT_DO_NEXT_ACTION), env);
        const bool held = step == 0 || step == 7;
        OBL_CHECK_EQ(env.sent.size(), size_t(1));
        CheckSelf(env.sent[0], MT_DO_NEXT_ACTION, 0);
        OBL_CHECK_EQ(env.delays[0], held ? static_cast<float>(0.7 * 20.0) : static_cast<float>(0.7));
        OBL_CHECK_EQ(answer, held ? svc::LowByte(77.25f) : svc::LowByte(static_cast<float>(0.7)));
        OBL_CHECK_EQ(member.instance.m_animType, held ? 0 : step == 1 || step == 8 ? 1 : 2);
        OBL_CHECK_EQ(member.object.m_actionState, static_cast<int32_t>((step + 1) % svc::kDjStepCount));
        anims += env.log;
    }
    OBL_CHECK_EQ(anims,
            std::string("Stand_dance1 Stand_to_dance1 Stand_to_mic Stand_que_deck1 Stand_mix Stand_flip_deck2 Stand_to_dance1 "
                        "Stand_dance1 Stand_to_dance1 Stand_to_mic Stand_que_deck2 Stand_mix Stand_flip_deck1 Stand_to_dance1 "));
    for (int32_t state : {14, -1}) {
        Member lost("DJ", state);
        FakeEnv env;
        const Message told = Told(MT_DO_NEXT_ACTION);
        OBL_CHECK_EQ(svc::ReceiveMessage(lost.object, told, env), svc::LowByte(told));
        OBL_CHECK(env.sent.empty() && env.log.empty());
        OBL_CHECK_EQ(lost.object.m_actionState, state);
    }
}

OBL_TEST_CASE("staff_service_receive.the_bar_serves_a_drink_and_polishes_a_glass")
{
    Member member("BARWOMAN", 0);
    FakeEnv serve;
    OBL_CHECK_EQ(svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 1), serve), svc::LowByte(1.5f));
    OBL_CHECK_EQ(serve.log, std::string("serve_drink "));
    CheckSelf(serve.sent[0], MT_PLAYANIMATION, 2);
    OBL_CHECK_EQ(serve.delays[0], 1.5f);
    OBL_CHECK_EQ(member.object.m_actionState, 1);

    FakeEnv done;
    const Message two = Told(MT_PLAYANIMATION, 2);
    OBL_CHECK_EQ(svc::ReceiveMessage(member.object, two, done), svc::LowByte(two));
    OBL_CHECK_EQ(done.log, std::string("stand "));
    OBL_CHECK(done.sent.empty());
    OBL_CHECK_EQ(member.object.m_actionState, 0);

    FakeEnv look;
    svc::ReceiveMessage(member.object, Told(MT_LOOK_AROUND), look);
    OBL_CHECK_EQ(look.log, std::string("pick_up_glass "));
    OBL_CHECK_EQ(look.sent.size(), size_t(2));
    CheckSelf(look.sent[0], MT_PLAYANIMATION, 3);
    OBL_CHECK_EQ(look.delays[0], 1.5f);
    CheckSelf(look.sent[1], MT_LOOK_AROUND, 0);
    OBL_CHECK_EQ(look.delays[1], 20.0f);
    OBL_CHECK_EQ(member.object.m_actionState, 3);

    FakeEnv clean;
    svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 3), clean);
    OBL_CHECK_EQ(clean.log, std::string("rand clean_glass "));
    CheckSelf(clean.sent[0], MT_PLAYANIMATION, 4);
    OBL_CHECK_EQ(member.object.m_actionState, 4);

    FakeEnv replace;
    svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 4), replace);
    OBL_CHECK_EQ(replace.log, std::string("replace_glass "));
    CheckSelf(replace.sent[0], MT_PLAYANIMATION, 5);
    OBL_CHECK_EQ(member.object.m_actionState, 5);

    FakeEnv stand;
    svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 5), stand);
    OBL_CHECK_EQ(stand.log, std::string("stand "));
    OBL_CHECK(stand.sent.empty());
    OBL_CHECK_EQ(member.object.m_actionState, 0);
}

OBL_TEST_CASE("staff_service_receive.an_odd_draw_blows_on_the_glass_and_a_step_out_of_turn_is_ignored")
{
    Member member("BARMAN", 3);
    FakeEnv blow;
    blow.draw = 7;
    svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 3), blow);
    OBL_CHECK_EQ(blow.log, std::string("rand blow_glass "));

    for (int data : {3, 4, 5}) {
        Member busy("BARMAN", 1);
        FakeEnv env;
        const Message told = Told(MT_PLAYANIMATION, data);
        OBL_CHECK_EQ(svc::ReceiveMessage(busy.object, told, env), svc::LowByte(told));
        OBL_CHECK(env.log.empty() && env.sent.empty());
        OBL_CHECK_EQ(busy.object.m_actionState, 1);
    }
    Member working("BARMAN", 1);
    FakeEnv look;
    svc::ReceiveMessage(working.object, Told(MT_LOOK_AROUND), look);
    OBL_CHECK(look.log.empty());
    OBL_CHECK_EQ(look.sent.size(), size_t(1)); // only the next look
    OBL_CHECK_EQ(working.object.m_actionState, 1);
}

OBL_TEST_CASE("staff_service_receive.the_kitchen_serves_catches_and_stands")
{
    Member member("CHEF", 6);
    FakeEnv serve;
    svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 0), serve);
    OBL_CHECK_EQ(serve.log, std::string("serve_burger "));
    CheckSelf(serve.sent[0], MT_PLAYANIMATION, 2);
    FakeEnv caught;
    svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 1), caught);
    OBL_CHECK_EQ(caught.log, std::string("catch_burger "));
    CheckSelf(caught.sent[0], MT_PLAYANIMATION, 0);
    FakeEnv stand;
    svc::ReceiveMessage(member.object, Told(MT_PLAYANIMATION, 2), stand);
    OBL_CHECK_EQ(stand.log, std::string("stand "));
    OBL_CHECK(stand.sent.empty());
    OBL_CHECK_EQ(member.object.m_actionState, 6); // the kitchen keeps no step

    Member other("DJ", 2);
    FakeEnv none;
    svc::ReceiveMessage(other.object, Told(MT_PLAYANIMATION, 1), none);
    OBL_CHECK(none.log.empty() && none.sent.empty());
    OBL_CHECK_EQ(other.object.m_actionState, 2);
}

OBL_TEST_CASE("staff_service_receive.other_messages_go_to_the_movement_of_the_base_class")
{
    Member member("BARMAN", 0);
    FakeEnv env;
    OBL_CHECK_EQ(svc::ReceiveMessage(member.object, Told(MT_NEW_HOUR), env), uint8_t(0x5a));
    OBL_CHECK_EQ(env.log, std::string("movement "));
}

} // namespace
