#include "unit_test.h"
#include "game/objects/world_create_building.h"
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
struct FakeEnv {
    obl::object::Building object{};
    tables::BuildingDef def{};
    ui::SpriteInstance sprite{};
    ui::Sprite spriteData{};
    ui::SpriteAnim anim{};
    ui::SpriteAnim* anims[1]{&anim};
    obl::messages::Message posted{};
    std::vector<std::string> calls;
    std::vector<int> tape{11, 22, 33};
    size_t next = 0;
    int buildings = 7;
    bool beer = true;
    FakeEnv()
    {
        object.m_id = 77;
        object.m_sprite = &sprite;
        sprite.m_sprite = &spriteData;
        spriteData.m_anims = anims;
        anim.m_directions = 4;
        def.m_GridWidth = 259;
        def.m_GridHeight = 258;
        def.m_NumCommodities = 5;
        def.m_Power = -100;
        def.m_EntranceOffset[0] = -3;
        def.m_EntranceOffset[1] = 4;
        def.m_ServeOffset[0] = 5;
        def.m_ServeOffset[1] = -6;
        for (int i = 0; i < 5; ++i) object.m_Commod_Price[i] = 10.0f + i;
    }
    const tables::BuildingDef& GetBuilding(const char* key)
    {
        OBL_CHECK_EQ(std::string(key), std::string("BAR"));
        calls.push_back("lookup");
        return def;
    }
    object::Base* CreateObjectByKey(const char* key, object::ObjectType type)
    {
        OBL_CHECK_EQ(std::string(key), std::string("BAR"));
        OBL_CHECK(type == object::ObjectType::Building);
        calls.push_back("create");
        OBL_CHECK_EQ(tape.at(next++), 11);
        return &object;
    }
    void SetType(object::Base*, object::ObjectType type)
    {
        OBL_CHECK(type == object::ObjectType::Building);
        OBL_CHECK(object.m_definition == &def);
        calls.push_back("type");
    }
    void SetState(object::Base*, int state)
    {
        OBL_CHECK_EQ(state, 0);
        calls.push_back("state");
    }
    void SetBuildingPosition(object::Base*, int x, int y)
    {
        OBL_CHECK_EQ(x, 20);
        OBL_CHECK_EQ(y, 30);
        calls.push_back("position");
        OBL_CHECK_EQ(tape.at(next++), 22);
    }
    void SetGridWidth(object::Base*, uint8_t width)
    {
        calls.push_back("width");
        object.m_gridW = width;
    }
    void SetGridHeight(object::Base*, uint8_t height)
    {
        calls.push_back("height");
        object.m_gridH = height;
    }
    void SetQueueDirection(object::Base*, int rotation) { calls.push_back("queue " + std::to_string(rotation)); }
    void FootprintPoint(
            float (&point)[3], const obl::object::BuildingCreateParams& r, const tables::BuildingDef& d, const int16_t (&offset)[2])
    {
        OBL_CHECK(&d == &def);
        OBL_CHECK_EQ(r.m_tileX, 20);
        OBL_CHECK_EQ(r.m_tileY, 30);
        const bool entrance = &offset == &def.m_EntranceOffset;
        calls.push_back(entrance ? "entrance-query" : "serve-query");
        point[0] = static_cast<float>(offset[0]);
        point[1] = static_cast<float>(offset[1]);
        point[2] = 0.25f;
    }
    void SetEntrancePoint(object::Base*, float x, float y, float z)
    {
        OBL_CHECK_EQ(x, static_cast<float>(def.m_EntranceOffset[0]));
        OBL_CHECK_EQ(y, static_cast<float>(def.m_EntranceOffset[1]));
        OBL_CHECK_EQ(z, 0.25f);
        calls.push_back("entrance");
    }
    void SetServePoint(object::Base*, float x, float y, float z)
    {
        OBL_CHECK_EQ(x, 5.0f);
        OBL_CHECK_EQ(y, -6.0f);
        OBL_CHECK_EQ(z, 0.25f);
        calls.push_back("serve");
    }
    bool BeerEnabled(int id)
    {
        OBL_CHECK_EQ(id, 2);
        calls.push_back("beer-lookup");
        return beer;
    }
    void SetBeerType(object::Base*, int id)
    {
        OBL_CHECK_EQ(id, 2);
        OBL_CHECK_EQ(object.m_Commod_Price[0], 2.5f);
        calls.push_back("beer-type");
    }
    void SetBeerStrength(object::Base*, float strength)
    {
        OBL_CHECK_EQ(strength, 0.75f);
        calls.push_back("beer-strength");
    }
    void SetBroken(object::Base*)
    {
        OBL_CHECK_EQ(object.m_BuildingAge, 19);
        object.m_id = 99;
        calls.push_back("broken");
    }
    void Post(obl::messages::Message& message)
    {
        posted = message;
        OBL_CHECK_EQ(buildings, 7);
        calls.push_back("post");
        OBL_CHECK_EQ(tape.at(next++), 33);
    }
    int& BuildingCount()
    {
        calls.push_back("count");
        return buildings;
    }
    void InsertLastBuildingByY()
    {
        OBL_CHECK_EQ(buildings, 8);
        calls.push_back("sort");
    }
};

obl::object::BuildingCreateParams Record()
{
    obl::object::BuildingCreateParams r{};
    std::strcpy(r.m_key, "BAR");
    r.m_tileX = 20;
    r.m_tileY = 30;
    r.m_orientation = 1;
    r.m_prices[0] = 2.5f;
    r.m_prices[1] = -1.0f;
    r.m_prices[2] = 0.0f;
    r.m_prices[3] = -0.0f;
    r.m_prices[4] = -2.0f;
    r.m_beerType = 2;
    r.m_beerStrength = 0.75f;
    r.m_age = 19;
    r.m_broken = 2;
    return r;
}
} // namespace

OBL_TEST_CASE("world_create_building.order_of_effects_messages_and_random_tape")
{
    FakeEnv env;
    OBL_CHECK(CreateBuilding(env, Record()) == &env.object);
    OBL_CHECK(env.calls ==
            std::vector<std::string>({"lookup", "create", "type", "state", "position", "width", "height", "queue 1", "entrance-query",
                    "entrance", "serve-query", "serve", "beer-lookup", "beer-type", "beer-strength", "broken", "post", "count", "sort"}));
    OBL_CHECK_EQ(env.next, size_t{3});
    OBL_CHECK_EQ(env.object.m_gridW, 2);
    OBL_CHECK_EQ(env.object.m_gridH, 3);
    OBL_CHECK_EQ(env.object.m_PowerConsumption, static_cast<uint32_t>(-100));
    OBL_CHECK_EQ(env.object.m_Commod_Price[0], 2.5f);
    OBL_CHECK_EQ(env.object.m_Commod_Price[1], 11.0f);
    OBL_CHECK_EQ(env.object.m_Commod_Price[2], 0.0f);
    OBL_CHECK_EQ(std::bit_cast<uint32_t>(env.object.m_Commod_Price[3]), 0x80000000u);
    OBL_CHECK_EQ(env.object.m_Commod_Price[4], -2.0f);
    OBL_CHECK_EQ(env.posted.sender, -2);
    OBL_CHECK_EQ(env.posted.receiver, 99);
    OBL_CHECK_EQ(env.posted.type, 3);
    OBL_CHECK_EQ(env.posted.flags, 0);
    OBL_CHECK_EQ(env.posted.data, 1);
}

OBL_TEST_CASE("world_create_building.optional_fields_preserve_constructor_defaults")
{
    FakeEnv env;
    auto record = Record();
    env.def.m_EntranceOffset[0] = env.def.m_EntranceOffset[1] = 0;
    env.def.m_NumCommodities = 0;
    record.m_beerType = -1;
    record.m_broken = 0;
    record.m_orientation = 2;
    CreateBuilding(env, record);
    OBL_CHECK(env.calls ==
            std::vector<std::string>({"lookup", "create", "type", "state", "position", "width", "height", "queue 2", "serve-query", "serve",
                    "post", "count", "sort"}));
    OBL_CHECK_EQ(env.object.m_gridW, 3);
    OBL_CHECK_EQ(env.object.m_gridH, 2);
    OBL_CHECK_EQ(env.posted.receiver, 77);
    OBL_CHECK_EQ(env.object.m_Commod_Price[0], 10.0f);
    OBL_CHECK_EQ(env.object.m_BuildingAge, 19);
}

OBL_TEST_CASE("world_create_building.disabled_beer_and_prices_beyond_commodity_count")
{
    FakeEnv env;
    auto record = Record();
    env.beer = false;
    env.def.m_NumCommodities = 1;
    CreateBuilding(env, record);
    OBL_CHECK_EQ(env.object.m_Commod_Price[0], 2.5f);
    OBL_CHECK_EQ(env.object.m_Commod_Price[2], 12.0f);
    OBL_CHECK(env.calls ==
            std::vector<std::string>({"lookup", "create", "type", "state", "position", "width", "height", "queue 1", "entrance-query",
                    "entrance", "serve-query", "serve", "beer-lookup", "broken", "post", "count", "sort"}));
}

OBL_TEST_CASE("world_create_building.sprite_clamp_does_not_change_requested_rotation")
{
    for (int rotation : {-1, 0, 1, 2, 3, 4, 255}) {
        FakeEnv env;
        auto record = Record();
        record.m_orientation = rotation;
        CreateBuilding(env, record);
        OBL_CHECK_EQ(env.sprite.m_direction, rotation < 4 ? rotation : 3);
        OBL_CHECK_EQ(env.object.m_gridW, (rotation & 1) ? 2 : 3);
        OBL_CHECK_EQ(env.calls[7], "queue " + std::to_string(rotation));
    }
}

OBL_TEST_CASE("world_create_building.either_nonzero_entrance_offset_enables_the_setter")
{
    for (int coordinate : {0, 1}) {
        FakeEnv env;
        env.def.m_EntranceOffset[coordinate] = 0;
        CreateBuilding(env, Record());
        OBL_CHECK_EQ(env.calls[8], std::string("entrance-query"));
        OBL_CHECK_EQ(env.calls[9], std::string("entrance"));
    }
}
