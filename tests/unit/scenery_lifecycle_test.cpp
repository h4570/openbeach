
#include "object_storage.h"
#include "unit_test.h"
#include "importer/src/readers/save/save_writer.h"

#include "game/objects/scenery_construction.h"
#include "game/objects/scenery_record.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace obl;
using obl::object::Scenery;
using obl::tables::SceneryDef;
using obl::ui::Sprite;
using obl::ui::SpriteAnim;
using obl::ui::SpriteInstance;

namespace {

// Stand in for the light / no-op routines (only their addresses matter; distinct bodies so identical-code folding keeps them apart).
void FakeLight(obl::object::Scenery& s)
{
    s.m_canSelect = 1;
}
void FakeNoOp(obl::object::Scenery& s)
{
    s.m_canSelect = 2;
}

struct FakeEnv {
    std::vector<std::string> log;
    SceneryDef def{};
    SpriteInstance instance{};
    Sprite sprite{};
    SpriteAnim anim{};
    SpriteAnim* anims[1] = {&anim};
    std::vector<int> rands{1000, 20000};
    size_t nextRand = 0;

    FakeEnv()
    {
        sprite.m_anims = anims;
        instance.m_sprite = &sprite;
        def.m_Sprite = "sprite";
        def.m_Menu = "None";
        def.m_GridWidth = 2;
        def.m_GridHeight = 3;
    }
    // What Prop's constructor (the base object's, in the game) does first: the key becomes the name.
    void BaseConstruct(Scenery& s, const char* key)
    {
        std::strncpy(s.m_Name, key, 31);
        log.push_back("base");
    }
    const SceneryDef* Definition(const char*) { return &def; }
    void SetGridSize(Scenery& s, uint8_t w, uint8_t h)
    {
        s.m_gridW = w;
        s.m_gridH = h;
    }
    SpriteInstance* CreateSprite(const char* name)
    {
        log.push_back(std::string("sprite ") + name);
        return &instance;
    }
    void SetAnim(SpriteInstance*, const char* name) { log.push_back(std::string("anim ") + name); }
    int Rand()
    {
        log.push_back("rand");
        return rands[nextRand++ % rands.size()];
    }
    Scenery::DrawFunc LightFunc() { return &FakeLight; }
    Scenery::DrawFunc NoOpFunc() { return &FakeNoOp; }
    const char* NoMenuText() { return "None"; }
    void ReleaseSprite(SpriteInstance*) { log.push_back("release"); }
};

void MakeFilled(Scenery& s)
{
    obl::unit::FillObject(s, 0x5a);
}

Scenery::DrawFunc DrawLightOf(const Scenery& s)
{
    return s.m_drawLight;
}

// Prop's constructor, then Scenery's.
Scenery* Build(Scenery& s, const char* key, FakeEnv& env)
{
    env.BaseConstruct(s, key);
    return scenery_construction::Construct(s, key, env);
}

} // namespace

OBL_TEST_CASE("scenery_construction.plain_object")
{
    FakeEnv env;
    env.def.m_Capacity = 4;
    Scenery s;
    MakeFilled(s);
    Build(s, "SHRUB1", env);
    OBL_CHECK(s.m_lightSprite == nullptr);
    OBL_CHECK(DrawLightOf(s) == &FakeNoOp);
    OBL_CHECK_EQ(s.m_gridW, 2);
    OBL_CHECK_EQ(s.m_gridH, 3);
    OBL_CHECK_EQ(s.m_MaxNumAttachedGuests, 1);
    OBL_CHECK_EQ(s.m_AttachedGuest[1], -1);
    OBL_CHECK_EQ(s.m_AttachedGuestState[1], -1);
    OBL_CHECK_EQ(s.m_NumItems, 0);
    OBL_CHECK_EQ(s.m_bakePending, 1);
    OBL_CHECK_EQ(s.m_hasMenu, 0);
    OBL_CHECK_EQ(s.m_canSelect, 0);
    OBL_CHECK_EQ(std::string(s.m_ObjectName), std::string("SHRUB1"));
    OBL_CHECK_EQ(env.instance.m_direction, -1); // the fake animation has no directions
}

OBL_TEST_CASE("scenery_construction.bench_has_two_seats_and_a_menu")
{
    FakeEnv env;
    env.def.m_Menu = "BenchMenu";
    env.anim.m_directions = 8;
    Scenery s;
    MakeFilled(s);
    Build(s, "bench", env);
    OBL_CHECK_EQ(s.m_MaxNumAttachedGuests, 2);
    OBL_CHECK_EQ(s.m_hasMenu, 1);
    OBL_CHECK_EQ(env.instance.m_direction, 0);
}

OBL_TEST_CASE("scenery_construction.street_lamp_draws_hours_in_order")
{
    FakeEnv env;
    Scenery s;
    MakeFilled(s);
    Build(s, "STREETLAMP", env);
    OBL_CHECK(s.m_lightSprite == &env.instance);
    OBL_CHECK(DrawLightOf(s) == &FakeLight);
    OBL_CHECK_EQ(s.m_lightsOn, scenery_construction::LightsOnHour(1000)); // the first draw is the lights-on hour
    OBL_CHECK_EQ(s.m_lightsOff, scenery_construction::LightsOffHour(20000));
    OBL_CHECK_EQ(env.log[1], std::string("sprite sprite")); // the definition's sprite comes first
    OBL_CHECK_EQ(env.log[2], std::string("sprite streetlightmap"));
}

OBL_TEST_CASE("scenery_construction.light_names_ignore_case")
{
    FakeEnv lower;
    Scenery a;
    MakeFilled(a);
    Build(a, "streetlamp", lower);
    OBL_CHECK(a.m_lightSprite != nullptr);
    FakeEnv fountain;
    Scenery b;
    MakeFilled(b);
    Build(b, "Fountain02", fountain);
    OBL_CHECK(b.m_lightSprite != nullptr);
    OBL_CHECK_EQ(fountain.log[2], std::string("sprite fountain02_Lightmap"));
}

OBL_TEST_CASE("scenery_construction.beach_fire_has_fixed_hours_and_no_random_draw")
{
    FakeEnv env;
    Scenery s;
    MakeFilled(s);
    Build(s, "BEACHFIRE", env);
    OBL_CHECK_EQ(s.m_lightsOn, 19.0f);
    OBL_CHECK_EQ(s.m_lightsOff, 6.5f);
    OBL_CHECK_EQ(env.nextRand, static_cast<size_t>(0));
}

OBL_TEST_CASE("scenery_construction.bin_animation_follows_the_capacity")
{
    FakeEnv env;
    env.def.m_Capacity = 10;
    Scenery s;
    MakeFilled(s);
    Build(s, "Bin", env);
    OBL_CHECK_EQ(env.log.back(), std::string("anim bin_emptybin_static"));
    FakeEnv zero; // capacity 0: fill == capacity, so the bin counts as full
    Scenery t;
    MakeFilled(t);
    Build(t, "Bin", zero);
    OBL_CHECK_EQ(zero.log.back(), std::string("anim bin_fullbin_static"));
    FakeEnv other;
    Scenery u;
    MakeFilled(u);
    other.def.m_Capacity = 10;
    Build(u, "BIN", other); // the comparison ignores case
    OBL_CHECK_EQ(other.log.back(), std::string("anim bin_emptybin_static"));
}

OBL_TEST_CASE("scenery_construction.destruct_releases_the_light_sprite")
{
    FakeEnv env;
    Scenery s;
    MakeFilled(s);
    Build(s, "STATUE01", env);
    env.log.clear();
    scenery_construction::Destruct(s, env);
    OBL_CHECK_EQ(env.log.size(), static_cast<size_t>(1));
    OBL_CHECK_EQ(env.log[0], std::string("release"));
    FakeEnv plain;
    s.m_lightSprite = nullptr;
    scenery_construction::Destruct(s, plain);
    OBL_CHECK_EQ(plain.log.size(), static_cast<size_t>(0)); // no light sprite
}

// ---- the footprint and the draw routine ------------------------------------------------------------------------------------------------------------------------

namespace {
struct RecordEnv {
    std::vector<std::pair<uint32_t, uint32_t>> cells;
    Scenery::DrawFunc DrawFor(scenery_record::Pick) { return nullptr; }
    void TilePosition(Scenery&, uint32_t& x, uint32_t& y)
    {
        x = 10;
        y = 20;
    }
    void AddToCell(uint32_t x, uint32_t y, Scenery&) { cells.emplace_back(x, y); }
    // A stand-in with the same shape as Grid::GetNeighbour: direction 0 steps one way, 6 the other.
    void Neighbour(uint32_t& x, uint32_t& y, int direction)
    {
        if (direction == 0)
            ++y;
        else
            ++x;
    }
};

struct Prepared {
    Sprite sprite{};
    SpriteInstance instance{};
    SceneryDef def{};
    Scenery s;
    Prepared()
    {
        MakeFilled(s);
        instance.m_sprite = &sprite;
        s.m_sprite = &instance;
        s.m_definition = &def;
        std::strcpy(s.m_Name, "Palm");
    }
};
} // namespace

OBL_TEST_CASE("scenery_record.load_joins_the_footprint_except_the_first_tile")
{
    Prepared p;
    p.s.m_gridW = 2;
    p.s.m_gridH = 2;
    RecordEnv env;
    scenery_record::JoinFootprint(p.s, env);
    // The block starts at the object's tile: (10,20) (10,21), then the next column (11,20) (11,21); the first tile is skipped.
    OBL_CHECK_EQ(env.cells.size(), static_cast<size_t>(3));
    OBL_CHECK(env.cells[0] == std::make_pair(10u, 21u));
    OBL_CHECK(env.cells[1] == std::make_pair(11u, 20u));
    OBL_CHECK(env.cells[2] == std::make_pair(11u, 21u));
}

OBL_TEST_CASE("scenery_record.load_of_an_empty_footprint_joins_nothing")
{
    Prepared p;
    p.s.m_gridW = 0;
    p.s.m_gridH = 3;
    RecordEnv env;
    scenery_record::JoinFootprint(p.s, env);
    OBL_CHECK(env.cells.empty());
    p.s.m_gridW = 3;
    p.s.m_gridH = 0;
    scenery_record::JoinFootprint(p.s, env);
    OBL_CHECK(env.cells.empty());
}

OBL_TEST_CASE("scenery_record.draw_routine_is_picked_by_sprite_type_then_name_then_burn")
{
    Prepared p;
    using scenery_record::Pick;
    OBL_CHECK(scenery_record::PickDraw(p.s) == Pick::Generic);
    p.def.m_burn = 1;
    OBL_CHECK(scenery_record::PickDraw(p.s) == Pick::Burned);
    std::strcpy(p.s.m_Name, "bench");
    OBL_CHECK(scenery_record::PickDraw(p.s) == Pick::Bench);
    std::strcpy(p.s.m_Name, "LOUNGER");
    OBL_CHECK(scenery_record::PickDraw(p.s) == Pick::Lounger);
    p.sprite.m_type = 5;
    OBL_CHECK(scenery_record::PickDraw(p.s) == Pick::Preview); // the sprite type beats the name
}

namespace {
// Every entry of the field table against the converter's Scenery bridge table (object type 1); a rectangle's converter keys
// carry the corner.
struct BridgeKeys {
    int checked = 0;
    int hashed = 0;
    void Check(const std::string& key, object::Hashed h)
    {
        OBL_CHECK_EQ(convert::OldObjectFieldKeyKnown(1, key) ? std::string() : key, std::string()); // names the unrecognized key
        ++checked;
        hashed += h == object::Hashed::Yes;
    }
    template<class T> void Int(const char* key, T&, object::Hashed h) { Check(key, h); }
    template<class T> void UInt(const char* key, T&, object::Hashed h) { Check(key, h); }
    void Float(const char* key, float&, object::Hashed h) { Check(key, h); }
    void Bool(const char* key, uint8_t&, object::Hashed h) { Check(key, h); }
    void Ints(const char* key, int32_t*, int, object::Hashed h) { Check(key, h); }
    void Rect(const char* key, screen::Rect&, object::Hashed h)
    {
        for (const char* corner : {".left", ".top", ".right", ".bottom"}) Check(std::string(key) + corner, h);
    }
};
} // namespace

OBL_TEST_CASE("scenery_record.field_table_keys_match_the_converter")
{
    Scenery s{};
    BridgeKeys keys;
    object::VisitFields(s, keys);
    OBL_CHECK_EQ(keys.checked, 22);
    OBL_CHECK_EQ(keys.hashed, 1); // the repair state, as the snapshot hashed it before the table
}
