#include "unit_test.h"
#include "object_storage.h"
#include "game/objects/entity_lifecycle.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace obl;
namespace {
using script::ScriptValue;
using ui::SpriteAnim;
using ui::SpriteInstance;
namespace life = entity::lifecycle;

struct Holder {
    alignas(obl::object::Mover) unsigned char bytes[sizeof(obl::object::Mover)]; // room for a derived class
    explicit Holder(unsigned char filler = 0xa5)
    {
        std::memset(bytes, filler, sizeof(bytes));
        obl::unit::StampVtable<object::Base>(bytes);
    }
    object::Base& Object() { return *reinterpret_cast<object::Base*>(bytes); }
};

// A sprite instance with one animation, as the draw offsets reach it: instance -> Sprite -> anims[animIndex].
struct SpriteFixture {
    SpriteAnim anim{};
    SpriteAnim* anims[2] = {nullptr, &anim};
    ui::Sprite sprite{};
    SpriteInstance instance{};
    char name[8] = "guest";
    SpriteFixture()
    {
        sprite.m_animCount = 2;
        sprite.m_anims = anims;
        instance.m_sprite = &sprite;
        instance.m_animIndex = 1;
        instance.m_name = name;
    }
};

struct FakeEnv {
    std::vector<std::string> calls;
    std::string fatalName;
    void* released = nullptr;
    uint32_t cellArgs[3] = {};
    int cell = 0;
    void* removedCell = nullptr;
    const object::Base* removedObject = nullptr;
    // GetValue
    std::vector<int> draws{7};
    std::map<intptr_t, uint32_t> memory;
    uint32_t startup = 0, gameTime = 0x41200000, weather = 2;
    // SetValue
    uint32_t position[3] = {};
    int drawOffsetX = 0;
    std::vector<int> drawOffsetXSeen;
    SpriteAnim* animToWatch = nullptr;

    void NameTooLong(const char* name)
    {
        calls.push_back("fatal");
        fatalName = name;
    }
    void ReleaseSprite(void* sprite)
    {
        calls.push_back("release");
        released = sprite;
    }
    void* CellAt(const uint32_t xyz[3])
    {
        calls.push_back("cell");
        std::memcpy(cellArgs, xyz, sizeof(cellArgs));
        return &cell;
    }
    void RemoveFromCell(void* c, object::Base& object)
    {
        calls.push_back("remove");
        removedCell = c;
        removedObject = &object;
    }
    int Rand()
    {
        calls.push_back("rand");
        const int value = draws.front();
        if (draws.size() > 1) draws.erase(draws.begin());
        return value;
    }
    uint32_t ReadWord(intptr_t address)
    {
        calls.push_back("read");
        return memory[address];
    }
    uint32_t WorldStartup() { return startup; }
    uint32_t GameTimeBits() { return gameTime; }
    uint32_t CurrentWeather() { return weather; }
    void SetPosition(object::Base& object, const uint32_t xyz[3])
    {
        calls.push_back("set_position");
        std::memcpy(position, xyz, sizeof(position));
        (void)object;
    }
    int GetDrawOffsetX(SpriteInstance*)
    {
        calls.push_back("draw_offset_x");
        drawOffsetXSeen.push_back(animToWatch ? animToWatch->m_hotspotX : 0);
        return drawOffsetX;
    }
};

std::string Joined(const std::vector<std::string>& calls)
{
    std::string text;
    for (const std::string& call : calls) text += (text.empty() ? "" : ", ") + call;
    return text;
}

// Every field of an object gets a distinct pattern (the pads stay as they are).
void Fill(object::Base& object, SpriteInstance* sprite)
{
    object.m_waitingForDeath = 1;
    object.m_requiresAttention = 1;
    std::strcpy(object.m_Name, "Beach Hut");
    object.m_id = 0x1234;
    object.m_type = object::ObjectType::Guest;
    object.m_owner = 7;
    object.m_WorldXYZ[0] = 12.5f;
    object.m_WorldXYZ[1] = -3.25f;
    object.m_WorldXYZ[2] = 6.0f;
    object.m_state = 41;
    object.m_visible = 1;
    object.m_sprite = sprite;
    object.m_XDrawOffset = -5;
    object.m_YDrawOffset = 9;
    object.m_debugBreak = 1;
    object.m_SelectLevel = 3;
}
} // namespace

OBL_TEST_CASE("base_lifecycle.construct_sets_the_defaults_and_leaves_the_pads")
{
    Holder h;
    FakeEnv env;
    object::Base* result = life::Construct(h.Object(), "guest", env);
    OBL_CHECK(result == &h.Object());
    const object::Base& o = h.Object();
    OBL_CHECK_EQ(std::string(o.m_Name), std::string("guest"));
    OBL_CHECK_EQ(int(o.m_Name[5]), 0);
    OBL_CHECK_EQ(int(o.m_Name[31]), 0);
    OBL_CHECK_EQ(o.m_id, 0xffffffffu);
    OBL_CHECK_EQ(int(o.m_visible), 1);
    OBL_CHECK(o.m_sprite == nullptr);
    OBL_CHECK_EQ(o.m_state, 0);
    OBL_CHECK_EQ(life::Bits(o.m_WorldXYZ[2]), 0u);
    OBL_CHECK_EQ(int(o.m_type), 0);
    OBL_CHECK_EQ(int(o.m_debugBreak), 0);
    OBL_CHECK_EQ(o.m_SelectLevel, 0);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("base_lifecycle.construct_names_of_every_length")
{
    for (size_t length : {size_t{0}, size_t{1}, size_t{4}, size_t{5}, size_t{30}, size_t{31}}) {
        Holder h;
        FakeEnv env;
        const std::string name(length, 'q');
        life::Construct(h.Object(), name.c_str(), env);
        OBL_CHECK_EQ(std::string(h.Object().m_Name), name);
        OBL_CHECK(env.calls.empty());
        // The terminator is part of the copy, so nothing of the filler is left behind it.
        for (size_t i = length; i < sizeof(h.Object().m_Name); ++i) OBL_CHECK_EQ(int(h.Object().m_Name[i]), 0);
    }
}

OBL_TEST_CASE("base_lifecycle.construct_a_name_of_32_characters_is_fatal_and_clamped")
{
    Holder h;
    FakeEnv env;
    const std::string name(40, 'z');
    life::Construct(h.Object(), name.c_str(), env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("fatal"));
    OBL_CHECK_EQ(env.fatalName, name);
    OBL_CHECK_EQ(int(h.Object().m_Name[31]), 'z');
    Holder ok;
    FakeEnv quiet;
    life::Construct(ok.Object(), std::string(31, 'y').c_str(), quiet);
    OBL_CHECK(quiet.calls.empty());
}

OBL_TEST_CASE("base_lifecycle.destruct_releases_the_sprite_then_leaves_the_cell")
{
    Holder h;
    SpriteFixture sprite;
    FakeEnv env;
    Fill(h.Object(), &sprite.instance);
    life::Destruct(h.Object(), env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("release, cell, remove"));
    OBL_CHECK(env.released == &sprite.instance);
    OBL_CHECK(h.Object().m_sprite == &sprite.instance); // the field keeps the dead pointer
    OBL_CHECK_EQ(env.cellArgs[0], std::bit_cast<uint32_t>(12.5f)); // the raw bits of x, y, z
    OBL_CHECK_EQ(env.cellArgs[1], std::bit_cast<uint32_t>(-3.25f));
    OBL_CHECK_EQ(env.cellArgs[2], std::bit_cast<uint32_t>(6.0f));
    OBL_CHECK(env.removedCell == &env.cell);
    OBL_CHECK(env.removedObject == &h.Object());
}

OBL_TEST_CASE("base_lifecycle.destruct_without_a_sprite_skips_the_release_and_keeps_nan_bits")
{
    Holder h;
    FakeEnv env;
    Fill(h.Object(), nullptr);
    std::memcpy(&h.Object().m_WorldXYZ[0], "\x01\x00\xa0\x7f", 4); // a signalling-looking NaN pattern
    life::Destruct(h.Object(), env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("cell, remove"));
    OBL_CHECK_EQ(env.cellArgs[0], 0x7fa00001u);
}

OBL_TEST_CASE("base_lifecycle.get_value_object_fields")
{
    Holder h;
    FakeEnv env;
    Fill(h.Object(), nullptr);
    auto get = [&](int32_t key, intptr_t data = 0x11223344) { return life::GetValue(h.Object(), key, data, env); };
    ScriptValue v = get(life::kKeyMe);
    OBL_CHECK_EQ(v.type, 2);
    OBL_CHECK_EQ(v.bits, 0x1234);
    v = get(life::kKeyState);
    OBL_CHECK_EQ(v.type, 2);
    OBL_CHECK_EQ(v.bits, 41);
    v = get(life::kKeyDrawOffsetX);
    OBL_CHECK_EQ(v.type, 2);
    OBL_CHECK_EQ(v.bits, -5);
    v = get(life::kKeyDrawOffsetY);
    OBL_CHECK_EQ(v.bits, 9);
    v = get(life::kKeyNeedsAttention); // a byte answer keeps the other three bytes of `data`
    OBL_CHECK_EQ(v.type, 0);
    OBL_CHECK_EQ(static_cast<uint32_t>(v.bits), 0x11223301u);
    h.Object().m_requiresAttention = 0;
    OBL_CHECK_EQ(static_cast<uint32_t>(get(life::kKeyNeedsAttention).bits), 0x11223300u);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("base_lifecycle.get_value_constants_and_world")
{
    Holder h;
    FakeEnv env;
    Fill(h.Object(), nullptr);
    auto get = [&](int32_t key, intptr_t data = 0x55667788) { return life::GetValue(h.Object(), key, data, env); };
    OBL_CHECK_EQ(get(life::kKeyNoObject).type, 2);
    OBL_CHECK_EQ(get(life::kKeyNoObject).bits, -1);
    OBL_CHECK_EQ(get(life::kKeyLightRain).bits, 3);
    OBL_CHECK_EQ(get(life::kKeyLightRain).type, 2);
    OBL_CHECK_EQ(static_cast<uint32_t>(get(life::kKeyTrue).bits), 0x55667701u);
    OBL_CHECK_EQ(static_cast<uint32_t>(get(life::kKeyFalse).bits), 0x55667700u);
    OBL_CHECK_EQ(get(life::kKeyCurWeather).bits, 2);
    env.weather = 5;
    OBL_CHECK_EQ(get(life::kKeyCurWeather).bits, 5);
    env.startup = 0; // the world has started
    OBL_CHECK_EQ(static_cast<uint32_t>(get(life::kKeyWorldStarted).bits), 0x55667701u);
    env.startup = 1;
    OBL_CHECK_EQ(static_cast<uint32_t>(get(life::kKeyWorldStarted).bits), 0x55667700u);
    const ScriptValue time = get(life::kKeyGameTime);
    OBL_CHECK_EQ(time.type, 3);
    OBL_CHECK_EQ(static_cast<uint32_t>(time.bits), 0x41200000u);
    const ScriptValue text = get(life::kKeyTextLiteral, 0x1000);
    OBL_CHECK_EQ(text.type, 1);
    OBL_CHECK_EQ(text.bits, obl::script::text::Id(reinterpret_cast<const char*>(uintptr_t{0x1000}))); // the literal's id
    // The data word is pointer-wide (intptr_t), so a real literal's address goes through whole and its id reads back the same text.
    static const char kLiteral[] = "Hello";
    const ScriptValue real = get(life::kKeyTextLiteral, reinterpret_cast<intptr_t>(kLiteral));
    OBL_CHECK(obl::script::text::Of(real.bits) == kLiteral);
}

OBL_TEST_CASE("base_lifecycle.get_value_numbers_and_random_read_the_operand_through_the_address")
{
    Holder h;
    FakeEnv env;
    env.memory[0x400] = 0x3fc00000; // 1.5f
    env.memory[0x404] = 10;
    env.memory[0x408] = 7;
    env.memory[0x40c] = 0;
    env.draws = {47};
    ScriptValue number = life::GetValue(h.Object(), life::kKeyNumber, 0x400, env);
    OBL_CHECK_EQ(number.type, 3);
    OBL_CHECK_EQ(static_cast<uint32_t>(number.bits), 0x3fc00000u);
    ScriptValue rand = life::GetValue(h.Object(), life::kKeyRandDword, 0x404, env); // 47 % 10
    OBL_CHECK_EQ(rand.type, 2);
    OBL_CHECK_EQ(rand.bits, 7);
    OBL_CHECK_EQ(Joined(env.calls), std::string("read, rand, read"));
    env.calls.clear();
    // The flag replaces the low byte of `data`, which here is the operand's address: the rest of the address stays in the answer.
    rand = life::GetValue(h.Object(), life::kKeyRandom, 0x408, env); // 47 % 7 = 5, not 0
    OBL_CHECK_EQ(rand.type, 0);
    OBL_CHECK_EQ(static_cast<uint32_t>(rand.bits), 0x400u);
    env.draws = {49}; // 49 % 7 = 0: the one-in-seven roll hit
    rand = life::GetValue(h.Object(), life::kKeyRandom, 0x408, env);
    OBL_CHECK_EQ(static_cast<uint32_t>(rand.bits), 0x401u);
    // The draw is unsigned: 0x80000000 % 10 = 8; a zero divisor does not trap.
    env.draws = {static_cast<int>(0x80000000u)};
    OBL_CHECK_EQ(life::GetValue(h.Object(), life::kKeyRandDword, 0x404, env).bits, 8);
    OBL_CHECK_EQ(life::GetValue(h.Object(), life::kKeyRandDword, 0x40c, env).bits, 0);
}

OBL_TEST_CASE("base_lifecycle.get_value_every_other_key_comes_back_as_it_was_asked")
{
    Holder h;
    FakeEnv env;
    Fill(h.Object(), nullptr);
    for (int32_t key : {0, 1, 0x35, 0x37, 0x4a, 0x54, 0x57, 0x5a, 0xb7, 0xb9, 0xc3, 0xc4, 0xe0, 0xeb, 0xef, 0xf0, 0xf1, 0xf2, 0xf5, 0x110,
                 -1, 0x7fffffff}) {
        const ScriptValue v = life::GetValue(h.Object(), key, 0x7654, env);
        OBL_CHECK_EQ(v.type, key);
        OBL_CHECK_EQ(v.bits, 0x7654);
    }
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("base_lifecycle.set_value_flags_take_the_low_byte")
{
    Holder h;
    FakeEnv env;
    Fill(h.Object(), nullptr);
    life::SetValue(h.Object(), life::kKeyWaitingForDeath, 0, 0, 0x1ff00, env);
    OBL_CHECK_EQ(int(h.Object().m_waitingForDeath), 0);
    life::SetValue(h.Object(), life::kKeyWaitingForDeath, 0, 0, 0x2, env);
    OBL_CHECK_EQ(int(h.Object().m_waitingForDeath), 2);
    life::SetValue(h.Object(), life::kKeyNeedsAttention, 0, 2, 0, env);
    OBL_CHECK_EQ(int(h.Object().m_requiresAttention), 0);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("base_lifecycle.set_value_world_position_moves_through_the_virtual")
{
    Holder h;
    FakeEnv env;
    Fill(h.Object(), nullptr);
    life::SetValue(h.Object(), life::kKeyWorldX, 0, 1, 100, env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("set_position"));
    OBL_CHECK_EQ(env.position[0], std::bit_cast<uint32_t>(100.0f));
    OBL_CHECK_EQ(env.position[1], std::bit_cast<uint32_t>(-3.25f));
    OBL_CHECK_EQ(env.position[2], std::bit_cast<uint32_t>(6.0f));
    OBL_CHECK_EQ(h.Object().m_WorldXYZ[0], 12.5f); // the object itself is not written here
    life::SetValue(h.Object(), life::kKeyWorldY, 0, 1, static_cast<int32_t>(0xfffffff0u), env); // an unsigned int: 4294967280
    OBL_CHECK_EQ(env.position[0], std::bit_cast<uint32_t>(12.5f));
    OBL_CHECK_EQ(env.position[1], std::bit_cast<uint32_t>(4294967280.0f));
    // The type of the value is not looked at.
    for (int type : {0, 1, 2, 3}) {
        life::SetValue(h.Object(), life::kKeyWorldX, 5, type, 3, env);
        OBL_CHECK_EQ(env.position[0], std::bit_cast<uint32_t>(3.0f));
    }
}

OBL_TEST_CASE("base_lifecycle.set_value_draw_offset_x_rewrites_the_hotspot")
{
    Holder h;
    SpriteFixture sprite;
    FakeEnv env;
    Fill(h.Object(), &sprite.instance);
    env.animToWatch = &sprite.anim;
    sprite.anim.m_hotspotX = 100;
    env.drawOffsetX = 40;
    h.Object().m_XDrawOffset = 6;
    life::SetValue(h.Object(), life::kKeyDrawOffsetX, 0, 3, std::bit_cast<int32_t>(-7.9f), env);
    OBL_CHECK_EQ(h.Object().m_XDrawOffset, -7); // truncates toward zero
    OBL_CHECK_EQ(Joined(env.calls), std::string("draw_offset_x, draw_offset_x"));
    // The first call takes the old offset back (40 + 6 into the hotspot), the second sees that hotspot and stores 40 - (-7).
    OBL_CHECK_EQ(env.drawOffsetXSeen[0], 100);
    OBL_CHECK_EQ(env.drawOffsetXSeen[1], 46);
    OBL_CHECK_EQ(int(sprite.anim.m_hotspotX), 47);
}

OBL_TEST_CASE("base_lifecycle.set_value_draw_offset_x_from_zero_skips_the_first_step")
{
    Holder h;
    SpriteFixture sprite;
    FakeEnv env;
    Fill(h.Object(), &sprite.instance);
    h.Object().m_XDrawOffset = 0;
    sprite.anim.m_hotspotX = 100;
    env.drawOffsetX = 30;
    life::SetValue(h.Object(), life::kKeyDrawOffsetX, 0, 3, std::bit_cast<int32_t>(12.0f), env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("draw_offset_x"));
    OBL_CHECK_EQ(h.Object().m_XDrawOffset, 12);
    OBL_CHECK_EQ(int(sprite.anim.m_hotspotX), 18);
    // The hotspot is a 16-bit word: it wraps.
    env.drawOffsetX = 32767;
    h.Object().m_XDrawOffset = 0;
    life::SetValue(h.Object(), life::kKeyDrawOffsetX, 0, 3, std::bit_cast<int32_t>(-10.0f), env);
    OBL_CHECK_EQ(int(sprite.anim.m_hotspotX), static_cast<int16_t>(32767 + 10));
}

OBL_TEST_CASE("base_lifecycle.set_value_draw_offset_y_moves_the_y_word_directly")
{
    Holder h;
    SpriteFixture sprite;
    FakeEnv env;
    Fill(h.Object(), &sprite.instance);
    sprite.anim.m_hotspotY = 50;
    h.Object().m_YDrawOffset = 9;
    life::SetValue(h.Object(), life::kKeyDrawOffsetY, 0, 3, std::bit_cast<int32_t>(4.0f), env);
    OBL_CHECK_EQ(h.Object().m_YDrawOffset, 4);
    OBL_CHECK_EQ(int(sprite.anim.m_hotspotY), 50 + 9 - 4);
    OBL_CHECK(env.calls.empty());
    h.Object().m_YDrawOffset = 0;
    sprite.anim.m_hotspotY = -32768;
    life::SetValue(h.Object(), life::kKeyDrawOffsetY, 0, 3, std::bit_cast<int32_t>(3.0f), env);
    OBL_CHECK_EQ(int(sprite.anim.m_hotspotY), 32765); // 16-bit wrap
}

OBL_TEST_CASE("base_lifecycle.set_value_other_keys_do_nothing")
{
    Holder h;
    Holder before;
    FakeEnv env;
    Fill(h.Object(), nullptr);
    std::memcpy(before.bytes, h.bytes, sizeof(h.bytes));
    for (int32_t key : {0, 0x36, 0x49, 0x4b, 0x58, 0xb7, 0xb9, 0xc2, 0xc5, 0xe1, 0xe2, 0xe9, 0xf2, 0xf5, 0x110, -1, 0x7fffffff})
        life::SetValue(h.Object(), key, 1, 2, 3, env);
    OBL_CHECK(std::memcmp(h.bytes, before.bytes, sizeof(h.bytes)) == 0);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("object_base.the_field_table_visits_the_head_records_in_save_order")
{
    struct Recorder {
        std::vector<std::string> log;
        void Bool(const char* name, uint8_t v) { log.push_back(std::string(name) + " bool " + std::to_string(v)); }
        void Name(const char* name, const char* v) { log.push_back(std::string(name) + " name " + v); }
        void Id(const char* name, uint32_t v) { log.push_back(std::string(name) + " id " + std::to_string(v)); }
        void Type(const char* name, object::ObjectType v)
        {
            log.push_back(std::string(name) + " type " + std::to_string(static_cast<int>(v)));
        }
        void Byte(const char* name, uint8_t v) { log.push_back(std::string(name) + " byte " + std::to_string(v)); }
        void Position(const object::Vec3& v)
        {
            for (int i = 0; i < 3; ++i)
                log.push_back(std::string(object::kPositionNames[i]) + " " + std::to_string(static_cast<int>(v[i])));
        }
        void Int(const char* name, int v) { log.push_back(std::string(name) + " int " + std::to_string(v)); }
    };
    Holder holder;
    object::Base& o = holder.Object();
    o.m_waitingForDeath = 1;
    o.m_requiresAttention = 0;
    std::strcpy(o.m_Name, "Lounger");
    o.m_id = 77;
    o.m_type = object::ObjectType::Scenery;
    o.m_owner = 3;
    o.m_WorldXYZ.x() = 10;
    o.m_WorldXYZ.y() = 20;
    o.m_WorldXYZ[2] = 30;
    o.m_state = -2;
    Recorder recorder;
    object::VisitHead(static_cast<const object::Base&>(o), recorder);
    OBL_CHECK((recorder.log ==
            std::vector<std::string>{"base.waiting_for_death bool 1", "base.requires_attention bool 0", "base.name name Lounger",
                    "base.id id 77", "base.type type 1", "base.owner byte 3", "base.position.x 10", "base.position.y 20",
                    "base.position.z 30", "base.state int -2"}));
    // The position still reads as the float[3] it replaced.
    const float* xyz = o.m_WorldXYZ;
    OBL_CHECK_EQ(xyz[1], 20.0f);
    OBL_CHECK_EQ(sizeof(o.m_Name), 32u);
}
