#include "unit_test.h"
#include "game/objects/world_create_guest.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_create_params.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_create;

namespace {
// The random tape: the draw the guest script makes while the first message is delivered comes before the factory's own.
struct FakeEnv {
    std::vector<std::string> calls;
    object::Base object{};
    tables::GuestDef def{};
    ui::SpriteInstance sprite{};
    obl::messages::Message posted{}, delayed{};
    float delay = -1.0f;
    int guests = 5;
    uint8_t keyline = 1;
    std::vector<int> tape;
    size_t next = 0;
    int postDraws = 0; // draws consumed by the delivery of the first message
    FakeEnv()
    {
        object.m_id = 77;
        def.m_Sprite = "GUY";
        def.m_DefSprite = 4;
    }
    const tables::GuestDef& GetGuest(const char* key)
    {
        calls.push_back(std::string("guest ") + key);
        return def;
    }
    object::Base* CreateObjectByKey(const char* key, object::ObjectType type)
    {
        calls.push_back(std::string("create ") + key + " " + std::to_string(static_cast<int>(type)));
        return &object;
    }
    void SetType(object::Base*, object::ObjectType type) { calls.push_back("type " + std::to_string(static_cast<int>(type))); }
    void Teleport(object::Base*, float x, float y, float z)
    {
        OBL_CHECK_EQ(x, 1.5f);
        OBL_CHECK_EQ(y, -2.0f);
        OBL_CHECK_EQ(z, 0.25f);
        calls.push_back("teleport");
    }
    void SetSprite(object::Base* o, const char* name)
    {
        calls.push_back(std::string("sprite ") + name);
        o->m_sprite = &sprite;
    }
    uint8_t GlobalKeyline()
    {
        calls.push_back("keyline");
        return keyline;
    }
    void Post(obl::messages::Message& msg)
    {
        posted = msg;
        calls.push_back("post");
        next += postDraws;
        object.m_id = 99; // the id is read again after the delivery
    }
    void PostDelayed(obl::messages::Message& msg, float d)
    {
        delayed = msg;
        delay = d;
        calls.push_back("post-delayed");
    }
    int Rand()
    {
        calls.push_back("rand");
        return tape.at(next++);
    }
    int& GuestCount()
    {
        calls.push_back("count");
        return guests;
    }
};

obl::object::GuestCreateParams Record()
{
    obl::object::GuestCreateParams r{};
    std::strcpy(r.m_key, "TOURIST");
    r.m_pos[0] = 1.5f;
    r.m_pos[1] = -2.0f;
    r.m_pos[2] = 0.25f;
    return r;
}
} // namespace

OBL_TEST_CASE("world_create_guest.order_of_effects_and_messages")
{
    FakeEnv env;
    env.tape = {16384};
    const auto r = Record();
    OBL_CHECK(CreateGuest(env, r) == &env.object);
    OBL_CHECK(env.calls ==
            std::vector<std::string>({"guest TOURIST", "create TOURIST 3", "type 3", "teleport", "sprite GUY", "keyline", "post", "rand",
                    "post-delayed", "count"}));
    OBL_CHECK_EQ(env.posted.sender, -2);
    OBL_CHECK_EQ(env.posted.receiver, 77);
    OBL_CHECK_EQ(env.posted.type, 3);
    OBL_CHECK_EQ(env.posted.flags, 0);
    OBL_CHECK_EQ(env.posted.data, 0);
    OBL_CHECK_EQ(env.delayed.sender, -2);
    OBL_CHECK_EQ(env.delayed.receiver, 99);
    OBL_CHECK_EQ(env.delayed.type, 63);
    OBL_CHECK_EQ(env.delayed.flags, 0);
    OBL_CHECK_EQ(env.delayed.data, 0);
    OBL_CHECK_EQ(env.guests, 6);
}

OBL_TEST_CASE("world_create_guest.sprite_animation_is_reset_only_when_it_differs")
{
    {
        FakeEnv env;
        env.tape = {0};
        env.sprite.m_animIndex = 1;
        env.sprite.m_frameIndex = 9;
        CreateGuest(env, Record());
        OBL_CHECK_EQ(env.sprite.m_animIndex, 4);
        OBL_CHECK_EQ(env.sprite.m_frameIndex, 0);
        OBL_CHECK_EQ(env.sprite.m_keyline, 1);
    }
    {
        FakeEnv env;
        env.tape = {0};
        env.sprite.m_animIndex = 4;
        env.sprite.m_frameIndex = 9;
        env.keyline = 0;
        env.sprite.m_keyline = 1;
        CreateGuest(env, Record());
        OBL_CHECK_EQ(env.sprite.m_animIndex, 4);
        OBL_CHECK_EQ(env.sprite.m_frameIndex, 9);
        OBL_CHECK_EQ(env.sprite.m_keyline, 0);
    }
}

OBL_TEST_CASE("world_create_guest.delay_uses_the_draw_after_the_delivery")
{
    FakeEnv env;
    env.tape = {111, 222, 32767};
    env.postDraws = 2;
    CreateGuest(env, Record());
    // Two draws were made while the guest started; the factory's own is the third.
    OBL_CHECK_EQ(env.next, size_t{3});
    OBL_CHECK_EQ(env.delay, 2.0f * static_cast<float>(32767.0 * 3.0518509447574615e-05f));
}

OBL_TEST_CASE("world_create_guest.opinion_delay_is_twice_the_scaled_draw")
{
    OBL_CHECK_EQ(OpinionDelay(0), 0.0f);
    OBL_CHECK(OpinionDelay(32767) > 1.99999f);
    OBL_CHECK(OpinionDelay(32767) < 2.00001f);
    OBL_CHECK_EQ(OpinionDelay(16384), static_cast<float>(2.0 * 16384.0 * static_cast<double>(3.0518509447574615e-05f)));
}
