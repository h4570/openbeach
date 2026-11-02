#include "unit_test.h"
#include "object_storage.h"
#include "importer/src/readers/save/save_writer.h"
#include "game/objects/mover_lifecycle.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace obl;
namespace {
using object::Mover;
using obl::object::Building;
using path::RouteCursor;
using script::ScriptValue;
namespace life = moving::lifecycle;

struct Holder {
    alignas(Mover) unsigned char bytes[sizeof(Mover)];
    explicit Holder(unsigned char filler = 0xa5)
    {
        std::memset(bytes, filler, sizeof(bytes));
        obl::unit::StampVtable<Mover>(bytes);
    }
    Mover& Object() { return *reinterpret_cast<Mover*>(bytes); }
};

// A building as GetValue reads it: the type byte, the entrance flag and the entrance point.
struct BuildingHolder {
    alignas(Building) unsigned char bytes[sizeof(Building)] = {};
    BuildingHolder() { obl::unit::StampVtable<obl::object::Building>(bytes); }
    obl::object::Building& Object() { return *reinterpret_cast<obl::object::Building*>(bytes); }
};

struct FakeEnv {
    std::vector<std::string> calls;
    int rand = 0;
    RouteCursor* released = nullptr;
    // GetValue
    std::vector<std::pair<int32_t, object::Base*>> objects;
    float servePoint[2] = {0, 0};
    ScriptValue base{};
    // SetValue
    int32_t toInt = 0;
    std::array<int32_t, 4> baseSet{};
    // Teleport
    uint32_t sceneryArgs[3] = {};
    uint32_t moveAtCall[2] = {};

    int Rand()
    {
        calls.push_back("rand");
        return rand;
    }
    void ReleaseCursor(RouteCursor* cursor)
    {
        calls.push_back("release");
        released = cursor;
    }
    object::Base* Object(int32_t id)
    {
        calls.push_back("object");
        for (auto& [key, object] : objects)
            if (key == id) return object;
        return nullptr;
    }
    void ServePosition(object::Base*, float out[2])
    {
        calls.push_back("serve");
        out[0] = servePoint[0];
        out[1] = servePoint[1];
    }
    ScriptValue Base(Mover&, int32_t key, intptr_t data)
    {
        calls.push_back("base_get " + std::to_string(key) + " " + std::to_string(data));
        return base;
    }
    void Base(Mover&, int32_t key, intptr_t data, int32_t type, int32_t bits)
    {
        calls.push_back("base_set");
        baseSet = {key, static_cast<int32_t>(data), type, bits};
    }
    int32_t ToInt(int32_t type, int32_t bits)
    {
        calls.push_back("to_int " + std::to_string(type) + " " + std::to_string(bits));
        return toInt;
    }
    void ScenerySetPosition(Mover& object, const uint32_t xyz[3])
    {
        calls.push_back("scenery");
        std::memcpy(moveAtCall, object.m_vPosition, sizeof(moveAtCall));
        std::memcpy(sceneryArgs, xyz, sizeof(sceneryArgs));
    }
};

std::string Joined(const std::vector<std::string>& calls)
{
    std::string text;
    for (const std::string& call : calls) text += (text.empty() ? "" : ", ") + call;
    return text;
}

} // namespace

OBL_TEST_CASE("moving_lifecycle.construct_sets_the_defaults_and_leaves_the_rest")
{
    Holder h;
    FakeEnv env;
    env.rand = 12345; // 12345 % 40 = 25, -20 = 5, +35 = 40
    life::Construct(h.Object(), env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("rand"));
    const Mover& o = h.Object();
    OBL_CHECK_EQ(o.m_mass, 5.0f);
    OBL_CHECK_EQ(o.m_maxForce, 15.0f);
    OBL_CHECK_EQ(o.m_maxVel, 40.0f);
    OBL_CHECK_EQ(o.m_targetTolerance, 1.5f);
    OBL_CHECK_EQ(o.m_behaviourState, 1u);
    OBL_CHECK_EQ(int(o.m_controlDirection), 1);
    OBL_CHECK_EQ(o.m_followId, -1);
    OBL_CHECK(o.m_pathCursor == nullptr);
    OBL_CHECK_EQ(o.m_vPosition[0], 0.0f);
    OBL_CHECK_EQ(o.m_DestXYZ[2], 0.0f);
    OBL_CHECK_EQ(o.m_lastDir, 0u);
    OBL_CHECK_EQ(o.m_GroupID, static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK_EQ(o.m_buildingId, static_cast<int32_t>(0xa5a5a5a5));
    OBL_CHECK_EQ(o.m_numWalkPts, 0xa5a5a5a5u);
    OBL_CHECK_EQ(o.m_currWalkPt, 0xa5a5a5a5u);
    OBL_CHECK_EQ(int(o.m_buildingTraversal), 0xa5);
}

OBL_TEST_CASE("moving_lifecycle.construct_top_speed_spans_15_to_54")
{
    for (const auto& [draw, expected] : {std::pair{0, 15.0f}, std::pair{39, 54.0f}, std::pair{40, 15.0f}, std::pair{32767, 15.0f + 7}}) {
        Holder h;
        FakeEnv env;
        env.rand = draw;
        life::Construct(h.Object(), env);
        OBL_CHECK_EQ(h.Object().m_maxVel, expected);
    }
}

OBL_TEST_CASE("moving_lifecycle.destruct_gives_back_the_route")
{
    Holder h;
    FakeEnv env;
    h.Object().m_pathCursor = nullptr;
    life::Destruct(h.Object(), env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("")); // no route: nothing to give back

    FakeEnv withRoute;
    RouteCursor cursor{};
    h.Object().m_pathCursor = &cursor;
    life::Destruct(h.Object(), withRoute);
    OBL_CHECK_EQ(Joined(withRoute.calls), std::string("release"));
    OBL_CHECK(withRoute.released == &cursor);
    OBL_CHECK(h.Object().m_pathCursor == &cursor); // the field is not cleared
}

OBL_TEST_CASE("moving_lifecycle.teleport_sets_the_move_position_and_hands_the_raw_words_on")
{
    Holder h;
    FakeEnv env;
    life::Teleport(h.Object(), 0x3f800000u, 0x40000000u, 0x40400000u, env);
    OBL_CHECK_EQ(Joined(env.calls), std::string("scenery"));
    OBL_CHECK_EQ(h.Object().m_vPosition[0], 1.0f);
    OBL_CHECK_EQ(h.Object().m_vPosition[1], 2.0f);
    // The move position is already set when the scenery set position routine runs; it gets the three arguments as they came.
    OBL_CHECK_EQ(env.moveAtCall[0], 0x3f800000u);
    OBL_CHECK_EQ(env.sceneryArgs[0], 0x3f800000u);
    OBL_CHECK_EQ(env.sceneryArgs[1], 0x40000000u);
    OBL_CHECK_EQ(env.sceneryArgs[2], 0x40400000u);
}

OBL_TEST_CASE("moving_lifecycle.teleport_quiets_a_signalling_nan_only_in_the_move_position")
{
    OBL_CHECK_EQ(life::QuietNaN(0x7fa12345u), 0x7fe12345u);
    OBL_CHECK_EQ(life::QuietNaN(0xffa00001u), 0xffe00001u);
    OBL_CHECK_EQ(life::QuietNaN(0x7fc00001u), 0x7fc00001u); // already quiet
    OBL_CHECK_EQ(life::QuietNaN(0x7f800000u), 0x7f800000u); // infinity
    OBL_CHECK_EQ(life::QuietNaN(0x00000001u), 0x00000001u); // denormal
    OBL_CHECK_EQ(life::QuietNaN(0x80000000u), 0x80000000u); // -0
    Holder h;
    FakeEnv env;
    life::Teleport(h.Object(), 0x7fa12345u, 0x3f800000u, 0x7fa00007u, env);
    OBL_CHECK_EQ(std::bit_cast<uint32_t>(h.Object().m_vPosition[0]), 0x7fe12345u);
    OBL_CHECK_EQ(env.sceneryArgs[0], 0x7fa12345u);
    OBL_CHECK_EQ(env.sceneryArgs[2], 0x7fa00007u);
}

OBL_TEST_CASE("moving_lifecycle.get_value_handles_exactly_the_five_numbers")
{
    for (int32_t key : {0x54, 0xef, 0xf0, 0xf1, 0xf2}) OBL_CHECK(life::HandlesGet(key));
    for (int32_t key : {0, 0x53, 0x55, 0xee, 0xf3, 0x110, -1, static_cast<int32_t>(0x80000000)}) OBL_CHECK(!life::HandlesGet(key));
    OBL_CHECK(life::HandlesSet(0xf0));
    for (int32_t key : {0, 0xef, 0xf1, -1}) OBL_CHECK(!life::HandlesSet(key));
}

OBL_TEST_CASE("moving_lifecycle.get_value_moving_keeps_the_upper_bytes_of_data")
{
    Holder h;
    FakeEnv env;
    h.Object().m_moving = 1;
    const ScriptValue value = life::GetValue(h.Object(), 0xef, 0x12345678, env);
    OBL_CHECK_EQ(value.type, 0);
    OBL_CHECK_EQ(static_cast<uint32_t>(value.bits), 0x12345601u);
    h.Object().m_moving = 0xff; // not normalised
    OBL_CHECK_EQ(static_cast<uint32_t>(life::GetValue(h.Object(), 0xef, 0, env).bits), 0xffu);
    OBL_CHECK(env.calls.empty());
}

OBL_TEST_CASE("moving_lifecycle.get_value_dest_object_is_the_id")
{
    Holder h;
    FakeEnv env;
    h.Object().m_DestObjectID = 77;
    const ScriptValue value = life::GetValue(h.Object(), 0xf0, 5, env);
    OBL_CHECK_EQ(value.type, 2);
    OBL_CHECK_EQ(value.bits, 77);
}

OBL_TEST_CASE("moving_lifecycle.get_value_dest_direction_reads_the_sprite_of_the_target")
{
    Holder h;
    FakeEnv env;
    ui::SpriteInstance sprite{};
    sprite.m_direction = 5;
    object::Base target{};
    target.m_sprite = &sprite;
    env.objects = {{9, &target}};
    h.Object().m_DestObjectID = 9;
    const ScriptValue value = life::GetValue(h.Object(), 0x54, 0, env);
    OBL_CHECK_EQ(value.type, 2);
    OBL_CHECK_EQ(value.bits, 5);
}

OBL_TEST_CASE("moving_lifecycle.get_value_entrance_coordinates_come_from_the_field_or_the_serve_point")
{
    Holder h;
    FakeEnv env;
    BuildingHolder building;
    building.Object().m_type = object::ObjectType::Building;
    building.Object().m_Entrance = 1;
    building.Object().m_EntrancePoint[0] = 12.9f;
    building.Object().m_EntrancePoint[1] = -3.9f;
    env.objects = {{3, &building.Object()}};
    h.Object().m_DestObjectID = 3;
    OBL_CHECK_EQ(life::GetValue(h.Object(), 0xf1, 0, env).bits, 12);
    OBL_CHECK_EQ(life::GetValue(h.Object(), 0xf2, 0, env).bits, -3); // truncated towards zero
    OBL_CHECK_EQ(life::GetValue(h.Object(), 0xf1, 0, env).type, 2);
    OBL_CHECK(std::find(env.calls.begin(), env.calls.end(), "serve") == env.calls.end());

    building.Object().m_Entrance = 0;
    env.servePoint[0] = 101.5f;
    env.servePoint[1] = 7.99f;
    OBL_CHECK_EQ(life::GetValue(h.Object(), 0xf1, 0, env).bits, 101);
    OBL_CHECK_EQ(life::GetValue(h.Object(), 0xf2, 0, env).bits, 7);
}

OBL_TEST_CASE("moving_lifecycle.get_value_entrance_of_something_else_returns_the_call")
{
    Holder h;
    FakeEnv env;
    object::Base guest{};
    guest.m_type = object::ObjectType::Guest;
    env.objects = {{4, &guest}};
    h.Object().m_DestObjectID = 4;
    ScriptValue value = life::GetValue(h.Object(), 0xf1, 0x55, env);
    OBL_CHECK_EQ(value.type, 0xf1);
    OBL_CHECK_EQ(value.bits, 0x55);
    value = life::GetValue(h.Object(), 0xf2, -9, env);
    OBL_CHECK_EQ(value.type, 0xf2);
    OBL_CHECK_EQ(value.bits, -9);
    h.Object().m_DestObjectID = 1234;
    value = life::GetValue(h.Object(), 0xf1, 3, env);
    OBL_CHECK_EQ(value.type, 0xf1);
}

OBL_TEST_CASE("moving_lifecycle.get_value_other_numbers_go_to_the_base_class_unchanged")
{
    Holder h;
    FakeEnv env;
    env.base = ScriptValue{3, 0x41200000};
    const ScriptValue value = life::GetValue(h.Object(), 0x53, 42, env);
    OBL_CHECK_EQ(value.type, 3);
    OBL_CHECK_EQ(value.bits, 0x41200000);
    OBL_CHECK_EQ(Joined(env.calls), std::string("base_get 83 42"));
}

OBL_TEST_CASE("moving_lifecycle.set_value_writes_only_dest_object")
{
    Holder h;
    FakeEnv env;
    env.toInt = 321;
    life::SetValue(h.Object(), 0xf0, 9, 3, 0x40a00000, env);
    OBL_CHECK_EQ(h.Object().m_DestObjectID, 321);
    OBL_CHECK_EQ(Joined(env.calls), std::string("to_int 3 1084227584"));

    FakeEnv other;
    life::SetValue(h.Object(), 0x4a, 7, 2, 11, other);
    OBL_CHECK_EQ(Joined(other.calls), std::string("base_set"));
    OBL_CHECK(other.baseSet == (std::array<int32_t, 4>{0x4a, 7, 2, 11}));
    OBL_CHECK_EQ(h.Object().m_DestObjectID, 321);
}

namespace {
// Every entry of the field table against the converter's Mover bridge table; the route entry is the `moving.path` group.
struct BridgeKeys {
    int checked = 0;
    int hashed = 0;
    void Check(const char* key, object::Hashed h)
    {
        OBL_CHECK_EQ(std::string(convert::OldMovingFieldKeyKnown(key) ? "" : key), std::string()); // names the unrecognized key
        ++checked;
        hashed += h == object::Hashed::Yes;
    }
    template<class T> void Int(const char* key, T&, object::Hashed h) { Check(key, h); }
    template<class T> void UInt(const char* key, T&, object::Hashed h) { Check(key, h); }
    void Float(const char* key, float&, object::Hashed h) { Check(key, h); }
    void Bool(const char* key, uint8_t&, object::Hashed h) { Check(key, h); }
    void Route(const char* key, path::RouteCursor*&, object::Hashed h)
    {
        OBL_CHECK(!convert::OldMovingFieldKeyKnown(key)); // the presence flag is dropped: the group carries the route
        OBL_CHECK(convert::OldMovingFieldKeyKnown(std::string(key) + ".current_step"));
        hashed += h == object::Hashed::Yes;
    }
};
} // namespace

OBL_TEST_CASE("moving_lifecycle.field_keys_match_the_save_bridge")
{
    BridgeKeys keys;
    Mover moving{};
    object::VisitFields(moving, keys);
    OBL_CHECK_EQ(keys.checked, 41);
    OBL_CHECK_EQ(keys.hashed, 19); // m_moving .. m_behaviourState and the route
}
