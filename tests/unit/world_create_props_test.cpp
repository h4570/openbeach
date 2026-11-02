#include "unit_test.h"
#include "game/objects/world_create_props.h"
#include "game/objects/object_create_params.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::world_create;

namespace {
struct FakeEnv {
    std::vector<std::string> calls;
    object::Base object{};
    obl::messages::Message posted{};
    int litter = 0;
    float scenic = 10.0f;
    FakeEnv() { object.m_id = 77; }
    int& LitterCount() { return litter; }
    float& ScenicValue() { return scenic; }
    float GetSceneryRating(const char* key)
    {
        calls.push_back(std::string("rating ") + key);
        return 2.5f;
    }
    void LookUpTable(const char* key) { calls.push_back(std::string("table ") + key); }
    const char* GetVehicleSprite(const char* key)
    {
        calls.push_back(std::string("vehicle ") + key);
        return "boat";
    }
    object::Base* CreateObjectByKey(const char* key, object::ObjectType type)
    {
        calls.push_back(std::string("create ") + key + " " + std::to_string(static_cast<int>(type)));
        return &object;
    }
    void SetName(object::Base*, const char* name) { calls.push_back(std::string("name ") + name); }
    void SetType(object::Base*, object::ObjectType type) { calls.push_back("type " + std::to_string(static_cast<int>(type))); }
    void SetState(object::Base*, int state) { calls.push_back("state " + std::to_string(state)); }
    void SetOwner(object::Base*, int id) { calls.push_back("owner " + std::to_string(id)); }
    void SetTilePosition(object::Base*, const uint32_t (&tile)[2])
    {
        calls.push_back("tile " + std::to_string(tile[0]) + " " + std::to_string(tile[1]));
    }
    void SetSpriteDirection(object::Base*, int direction) { calls.push_back("direction " + std::to_string(direction)); }
    void AttachTable(object::Base*, int id) { calls.push_back("attach " + std::to_string(id)); }
    void SetWorldXYZ(object::Base*, float x, float y, float z) { Position("xyz", x, y, z); }
    void Teleport(object::Base*, float x, float y, float z) { Position("teleport", x, y, z); }
    void Position(const char* method, float x, float y, float z)
    {
        OBL_CHECK_EQ(x, 1.5f);
        OBL_CHECK_EQ(y, -2.0f);
        OBL_CHECK_EQ(z, 0.25f);
        calls.push_back(method);
    }
    void SetTableOrientation(object::Base*, int value)
    {
        calls.push_back("orientation " + std::to_string(value));
        object.m_id = 99;
    }
    void SetVehicleHome(object::Base*, int id) { calls.push_back("home " + std::to_string(id)); }
    void SetSprite(object::Base*, const char* name) { calls.push_back(std::string("sprite ") + name); }
    void SetVehicleDirection(object::Base*, int value)
    {
        calls.push_back("vehicle-direction " + std::to_string(value));
        object.m_id = 99;
    }
    void Post(obl::messages::Message& msg)
    {
        posted = msg;
        calls.push_back("post");
    }
};

obl::object::SceneryCreateParams SceneryRecord()
{
    obl::object::SceneryCreateParams r{};
    std::strcpy(r.m_key, "TREE");
    std::strcpy(r.m_name, "Oak");
    r.m_tile[0] = 12;
    r.m_tile[1] = 34;
    r.m_ownerBuildId = -2;
    return r;
}

void CheckMessage(const obl::messages::Message& msg, int receiver)
{
    OBL_CHECK_EQ(msg.sender, -2);
    OBL_CHECK_EQ(msg.receiver, receiver);
    OBL_CHECK_EQ(msg.type, 3);
    OBL_CHECK_EQ(msg.flags, 0);
    OBL_CHECK_EQ(msg.data, 1);
}
} // namespace

OBL_TEST_CASE("world_create_props.scenery_rates_before_allocation_and_preserves_optional_fields")
{
    FakeEnv env;
    auto r = SceneryRecord();
    OBL_CHECK(CreateScenery(env, r) == &env.object);
    OBL_CHECK_EQ(env.scenic, 12.5f);
    OBL_CHECK_EQ(env.litter, 0);
    OBL_CHECK(
            env.calls == std::vector<std::string>({"rating TREE", "create TREE 1", "name Oak", "type 1", "state 0", "tile 12 34", "post"}));
    CheckMessage(env.posted, 77);
    env.calls.clear();
    r.m_ownerBuildId = 42;
    r.m_setDirection = 2;
    r.m_direction = 255;
    CreateScenery(env, r);
    OBL_CHECK(env.calls ==
            std::vector<std::string>(
                    {"rating TREE", "create TREE 1", "name Oak", "type 1", "state 0", "owner 42", "tile 12 34", "direction 255", "post"}));
}

OBL_TEST_CASE("world_create_props.litter_matches_a_substring_in_any_case_and_caps_before_any_effect")
{
    auto r = SceneryRecord();
    std::strcpy(r.m_name, "SomeLitterHere");
    for (int count : {0, 499, 500, 501, -1}) {
        FakeEnv env;
        env.litter = count;
        auto* result = CreateScenery(env, r);
        if (count == 0 || count == 499) {
            OBL_CHECK(result == &env.object);
            OBL_CHECK_EQ(env.litter, count + 1);
            OBL_CHECK_EQ(env.calls.front(), "create TREE 1");
        } else {
            OBL_CHECK(result == nullptr);
            OBL_CHECK(env.calls.empty());
            OBL_CHECK_EQ(env.litter, count);
        }
        OBL_CHECK_EQ(env.scenic, 10.0f);
    }
    FakeEnv env;
    std::strcpy(r.m_name, "litter");
    CreateScenery(env, r);
    OBL_CHECK_EQ(env.litter, 1);
    OBL_CHECK_EQ(env.scenic, 10.0f);
}

OBL_TEST_CASE("world_create_props.table_attaches_before_position_and_has_no_initial_state_setter")
{
    FakeEnv env;
    obl::object::TableCreateParams r{};
    std::strcpy(r.m_key, "TABLE");
    r.m_pos[0] = 1.5f;
    r.m_pos[1] = -2.0f;
    r.m_pos[2] = 0.25f;
    r.m_buildingId = 42;
    r.m_orientation = 3;
    OBL_CHECK(CreateTable(env, r) == &env.object);
    OBL_CHECK(env.calls ==
            std::vector<std::string>({"table TABLE", "create TABLE 5", "attach 42", "type 5", "xyz", "orientation 3", "post"}));
    CheckMessage(env.posted, 99);
}

OBL_TEST_CASE("world_create_props.vehicle_sets_home_before_type_and_records_the_requested_direction")
{
    FakeEnv env;
    obl::object::VehicleCreateParams r{};
    std::strcpy(r.m_key, "BOAT");
    r.m_pos[0] = 1.5f;
    r.m_pos[1] = -2.0f;
    r.m_pos[2] = 0.25f;
    r.m_homeId = 42;
    r.m_direction = 255;
    OBL_CHECK(CreateVehicle(env, r) == &env.object);
    OBL_CHECK(env.calls ==
            std::vector<std::string>({"vehicle BOAT", "create BOAT 4", "home 42", "type 4", "state 1", "teleport", "sprite boat",
                    "direction 255", "vehicle-direction 255", "post"}));
    CheckMessage(env.posted, 99);
}

OBL_TEST_CASE("world_create_props.sprite_direction_clamps_only_the_upper_bound_of_the_current_animation")
{
    ui::SpriteAnim a{}, b{};
    a.m_directions = 8;
    b.m_directions = 4;
    ui::SpriteAnim* anims[] = {&a, &b};
    ui::Sprite sprite{};
    sprite.m_anims = anims;
    ui::SpriteInstance instance{};
    instance.m_sprite = &sprite;
    instance.m_animIndex = 1;
    for (int direction : {-5, 0, 3, 4, 255}) {
        SetSpriteDirection(instance, direction);
        OBL_CHECK_EQ(instance.m_direction, direction < 4 ? direction : 3);
    }
    b.m_directions = 0;
    SetSpriteDirection(instance, 0);
    OBL_CHECK_EQ(instance.m_direction, -1);
}
