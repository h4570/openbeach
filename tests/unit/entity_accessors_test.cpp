#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/entity_accessors.h"
#include "game/objects/object_base.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl;
namespace {
namespace acc = entity::accessors;

struct Holder {
    alignas(object::Base) unsigned char bytes[sizeof(object::Base)];
    Holder()
    {
        std::memset(bytes, 0xa5, sizeof(bytes));
        obl::unit::StampVtable<object::Base>(bytes);
    }
    object::Base& Object() { return *reinterpret_cast<object::Base*>(bytes); }
};

uint32_t Bits(const float& value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

const float kFive = 5.0f, kA = 1.5f, kB = 2.5f, kC = 3.5f;

struct FakeEnv {
    std::vector<std::string> calls;
    float worldSeen[3] = {};
    int tile[2] = {7, -3};
    bool postResult = true;
    acc::WireMessage posted{};
    uint32_t delaySeen = 0;
    std::string keySeen;
    uint32_t typeSeen = 0;
    const void* spriteSeen = nullptr;
    const float* worldPointer = nullptr;
    uint32_t timeSeen = 0;

    void WorldToTile(const float world[3], int32_t out[2])
    {
        calls.push_back("tile");
        std::memcpy(worldSeen, world, sizeof(worldSeen));
        out[0] = tile[0];
        out[1] = tile[1];
    }
    bool Post(const acc::WireMessage& message)
    {
        calls.push_back("post");
        posted = message;
        return postResult;
    }
    bool PostDelayed(const acc::WireMessage& message, uint32_t delay)
    {
        calls.push_back("delayed");
        posted = message;
        delaySeen = delay;
        return postResult;
    }
    void InvalidKeyword(const char* key, uint32_t type)
    {
        calls.push_back("fatal");
        keySeen = key;
        typeSeen = type;
    }
    uint32_t GameTimeBits() { return 0x40490fdb; }
    void DrawInWorld(void* sprite, const float* world, uint32_t time)
    {
        calls.push_back("draw");
        spriteSeen = sprite;
        worldPointer = world;
        timeSeen = time;
    }
};
} // namespace

OBL_TEST_CASE("base_accessors.setters_touch_only_their_field")
{
    Holder h;
    acc::SetType(h.Object(), 0xcd);
    OBL_CHECK_EQ(int(static_cast<uint8_t>(h.Object().m_type)), 0xcd);
    OBL_CHECK_EQ(int(h.Object().m_owner), 0xa5);
    acc::SetId(h.Object(), 0xdeadbeefu);
    OBL_CHECK_EQ(h.Object().m_id, 0xdeadbeefu);
    acc::SetState(h.Object(), 0x80000001u);
    OBL_CHECK_EQ(static_cast<uint32_t>(h.Object().m_state), 0x80000001u);
    OBL_CHECK_EQ(int(h.Object().m_visible), 0xa5);
}

OBL_TEST_CASE("base_accessors.set_world_keeps_nan_patterns_and_the_state")
{
    Holder h;
    acc::SetWorldXYZ(h.Object(), 0x7fa00001u, 0xffc12345u, 0x3f800000u);
    OBL_CHECK_EQ(Bits(h.Object().m_WorldXYZ[0]), 0x7fa00001u);
    OBL_CHECK_EQ(Bits(h.Object().m_WorldXYZ[1]), 0xffc12345u);
    OBL_CHECK_EQ(Bits(h.Object().m_WorldXYZ[2]), 0x3f800000u);
    OBL_CHECK_EQ(static_cast<uint32_t>(h.Object().m_state), 0xa5a5a5a5u);
}

OBL_TEST_CASE("base_accessors.clear_selection_zeroes_the_level_only")
{
    Holder h;
    acc::ClearSelection(h.Object());
    OBL_CHECK_EQ(h.Object().m_SelectLevel, 0);
    OBL_CHECK_EQ(int(h.Object().m_debugBreak), 0xa5);
}

OBL_TEST_CASE("base_accessors.get_position_copies_three_dwords_and_returns_out")
{
    Holder h;
    acc::SetWorldXYZ(h.Object(), 0x7fc00001u, 0x40000000u, 0xbf800000u);
    float out[4] = {0, 0, 0, 5.0f};
    OBL_CHECK(acc::GetPosition(h.Object(), out) == out);
    OBL_CHECK_EQ(Bits(out[0]), 0x7fc00001u);
    OBL_CHECK_EQ(Bits(out[1]), 0x40000000u);
    OBL_CHECK_EQ(Bits(out[2]), 0xbf800000u);
    OBL_CHECK_EQ(Bits(out[3]), Bits(kFive));
}

OBL_TEST_CASE("base_accessors.get_tile_converts_the_position_and_returns_out")
{
    Holder h;
    acc::SetWorldXYZ(h.Object(), Bits(kA), Bits(kB), Bits(kC));
    FakeEnv env;
    int32_t out[2] = {0, 0};
    OBL_CHECK(acc::GetTilePosition(h.Object(), out, env) == out);
    OBL_CHECK_EQ(out[0], 7);
    OBL_CHECK_EQ(out[1], -3);
    OBL_CHECK_EQ(Bits(env.worldSeen[0]), Bits(kA));
    OBL_CHECK_EQ(Bits(env.worldSeen[2]), Bits(kC));
    OBL_CHECK_EQ(int(env.calls.size()), 1);
}

OBL_TEST_CASE("base_accessors.has_basic_state_is_state_at_most_one_unsigned")
{
    OBL_CHECK(acc::HasBasicState(0));
    OBL_CHECK(acc::HasBasicState(1));
    OBL_CHECK(!acc::HasBasicState(2));
    OBL_CHECK(!acc::HasBasicState(0x80000000u));
    OBL_CHECK(!acc::HasBasicState(0xffffffffu));
}

OBL_TEST_CASE("base_accessors.play_animation_reports_zero")
{
    const float duration = acc::PlayAnimation();
    OBL_CHECK_EQ(Bits(duration), 0u);
}

OBL_TEST_CASE("base_accessors.senders_pass_the_message_and_the_delay_bits")
{
    acc::WireMessage message{};
    for (uint32_t i = 0; i < 8; ++i) message.words[i] = 0x7fc00000u + i;
    FakeEnv env;
    OBL_CHECK(acc::SendMessage(message, env));
    OBL_CHECK_EQ(env.posted.words[7], 0x7fc00007u);
    env.postResult = false;
    OBL_CHECK(!acc::SendMessageDelayed(message, 0x7fc0abcdu, env));
    OBL_CHECK_EQ(env.delaySeen, 0x7fc0abcdu);
    OBL_CHECK_EQ(env.posted.words[0], 0x7fc00000u);
    OBL_CHECK_EQ(int(env.calls.size()), 2);
}

OBL_TEST_CASE("base_accessors.get_variable_text_is_a_fatal_error_with_key_and_type")
{
    Holder h;
    h.Object().m_type = object::ObjectType::Guest;
    FakeEnv env;
    acc::GetVariableText(h.Object(), "BAD_KEY", env);
    OBL_CHECK_EQ(env.keySeen, std::string("BAD_KEY"));
    OBL_CHECK_EQ(env.typeSeen, 3u);
    OBL_CHECK_EQ(int(env.calls.size()), 1);
}

OBL_TEST_CASE("base_accessors.draw_hands_the_sprite_the_position_pointer_and_the_game_time")
{
    Holder h;
    int sprite = 0;
    h.Object().m_sprite = &sprite;
    FakeEnv env;
    acc::Draw(h.Object(), env);
    OBL_CHECK(env.spriteSeen == &sprite);
    OBL_CHECK(env.worldPointer == h.Object().m_WorldXYZ);
    OBL_CHECK_EQ(env.timeSeen, 0x40490fdbu);
}
