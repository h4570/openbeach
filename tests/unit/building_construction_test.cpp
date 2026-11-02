#include "object_storage.h"
#include "unit_test.h"

#include "importer/src/readers/building_behavior_keys.h"
#include "game/objects/building_construction.h"

#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using object::Building;
using tables::BeerDef;
using tables::BuildingDef;
using ui::SpriteInstance;
namespace cons = building_construction;

namespace {

struct FakeSprite {
    SpriteInstance instance{};
    ui::Sprite sprite{};
    std::string name;
};

// Records the order of the calls that leave the object; `rands` are what the RNG hands out, in order.
struct Env {
    std::vector<std::string> calls;
    std::deque<int> rands;
    std::vector<std::unique_ptr<FakeSprite>> sprites;
    BuildingDef definition{};
    BeerDef beers[3] = {};
    int numBeers = 3;
    const char* commodityNames[8] = {};
    int families[8] = {};
    int alcohol = 5;
    int32_t dayTime = 17;
    std::vector<void*> freed;

    // What Prop's constructor (the base object's, in the game) does first: the key becomes the name.
    void ConstructBase(Building& builder, const char* key)
    {
        calls.push_back("base " + std::string(key));
        std::strncpy(builder.m_Name, key, sizeof(builder.m_Name) - 1);
    }
    const BuildingDef* Definition(const char* key)
    {
        definition.m_Behavior = BehaviorFromKey(key);
        return &definition;
    }
    int32_t DayTimeTicks() { return dayTime; }
    int Rand()
    {
        calls.push_back("rand");
        const int value = rands.front();
        rands.pop_front();
        return value;
    }
    int CommodityFamily(int id) { return families[id]; }
    int AlcoholFamilyId() { return alcohol; }
    const BeerDef* Beer(int id) { return &beers[id]; }
    int BeerCount() { return numBeers; }
    const char* CommodityDisplayName(int id) { return commodityNames[id]; }
    SpriteInstance* CreateSprite(const char* name)
    {
        calls.push_back(std::string("sprite ") + name);
        sprites.push_back(std::make_unique<FakeSprite>());
        sprites.back()->name = name;
        sprites.back()->instance.m_sprite = &sprites.back()->sprite;
        return &sprites.back()->instance;
    }
};

struct Fixture {
    Env env;
    Building object;
    Fixture()
    {
        obl::unit::FillObject(object, 0xa5);
        env.rands = {3, 16384, 32767};
        env.definition.m_Sprite = "BAR";
        env.beers[0].m_BaseCost = 2.0f;
        env.beers[0].m_StrCost = 4.0f;
        env.beers[0].m_Price = 3.5f;
        env.beers[0].m_scriptFlag = 1;
        env.beers[1] = env.beers[0];
        env.beers[1].m_BaseCost = 1.0f;
        env.beers[2] = env.beers[1];
    }
    void Run(const char* key = "BAR")
    {
        env.ConstructBase(object, key);
        cons::Construct(object, key, env);
    }
};

} // namespace

OBL_TEST_CASE("building_construction.order_and_rng")
{
    Fixture f;
    f.env.definition.m_NumAnimSprites = 1;
    f.env.definition.m_AnimSprites[0].m_Sprite = "FLAG";
    f.Run();
    const std::vector<std::string> expected = {
            "base BAR", "rand", "sprite BAR", "sprite FLAG", "sprite UI3_fb_shortage", "sprite UI3_fb_spanner", "rand", "rand"};
    OBL_CHECK_EQ(f.env.calls.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < f.env.calls.size(); ++i) OBL_CHECK_EQ(f.env.calls[i], expected[i]);
    OBL_CHECK_EQ(f.object.m_RepairInterval, 4);
    OBL_CHECK_EQ(f.object.m_UserRepairInterval, 8);
    OBL_CHECK(f.object.m_lightsOn > 19.5f + 0.4999f && f.object.m_lightsOn < 19.5f + 0.5001f);
    OBL_CHECK(f.object.m_lightsOff > 5.5f + 0.9999f && f.object.m_lightsOff < 5.5f + 1.0001f);
    OBL_CHECK_EQ(f.object.m_LastRepairCall, 17);
    OBL_CHECK_EQ(f.object.m_LastFailTime, 17);
    OBL_CHECK_EQ(f.object.m_InfluenceArea, -1);
    OBL_CHECK(f.object.m_definition == &f.env.definition);
}

OBL_TEST_CASE("building_construction.defaults")
{
    Fixture f;
    f.Run();
    OBL_CHECK_EQ(f.object.m_OpenStatus, 1u);
    OBL_CHECK_EQ(f.object.m_burn, 1);
    OBL_CHECK_EQ(f.object.m_Entrance, 0);
    OBL_CHECK_EQ(f.object.m_InfluenceArea, -1);
    OBL_CHECK(f.object.m_lastFaultDayTime == -1.0f);
    OBL_CHECK(f.object.m_lastRepairDayTime == -1.0f);
    OBL_CHECK(f.object.m_queue_direction[0] == -12.0f && f.object.m_queue_direction[1] == 12.0f);
    OBL_CHECK(f.object.m_queue == nullptr && f.object.m_events == nullptr && f.object.m_constructionSprite == nullptr);
    OBL_CHECK_EQ(f.object.m_NumLilos, 0);
    OBL_CHECK_EQ(f.object.m_walkPoints == nullptr, true);
    for (float value : f.object.m_profitRecord) OBL_CHECK(value == 0.0f);
    for (auto* sprite : f.object.m_animSprites) OBL_CHECK(sprite == nullptr);
    OBL_CHECK_EQ(f.object.m_shortageSprite->m_fps, 2);
    OBL_CHECK_EQ(f.object.m_spannerSprite->m_fps, 2);
    OBL_CHECK_EQ(f.object.m_BeerType, -1);
}

OBL_TEST_CASE("building_construction.screen_rects_start_empty")
{
    Fixture f;
    f.Run();
    const auto empty = [](const obl::screen::Rect& r) { return r.left == 0 && r.top == 0 && r.right == 0 && r.bottom == 0; };
    OBL_CHECK(empty(f.object.m_rScrExtents));
    OBL_CHECK(empty(f.object.m_rScrRect));
}

OBL_TEST_CASE("building_construction.equipment_hire_and_extras")
{
    Fixture f;
    f.env.definition.m_NumAnimSprites = 2;
    f.env.definition.m_AnimSprites[0].m_Sprite = "beachdisco_anim";
    f.env.definition.m_AnimSprites[1].m_Sprite = "OTHER";
    f.env.definition.m_customConstruct = 1;
    f.env.definition.m_Construct = "SITE";
    f.env.definition.m_walkPoints = {{1, 2}, {3, 4}};
    f.env.definition.m_NumWalkPoints = 2;
    f.Run("EQPTHIRE");
    OBL_CHECK_EQ(f.object.m_NumLilos, 20);
    OBL_CHECK_EQ(f.object.m_NumBoards, 20);
    OBL_CHECK_EQ(f.object.m_NumSnorkels, 20);
    OBL_CHECK_EQ(f.env.sprites[1]->sprite.m_type, 0xc); // BEACHDISCO_ANIM compared without case
    OBL_CHECK_EQ(f.env.sprites[2]->sprite.m_type, 0);
    OBL_CHECK(f.object.m_animSprites[1] == &f.env.sprites[2]->instance);
    OBL_CHECK(f.object.m_constructionSprite == &f.env.sprites[3]->instance);
    OBL_CHECK_EQ(f.env.sprites[3]->name, "SITE");
    OBL_CHECK(f.object.m_walkPoints == f.env.definition.m_walkPoints.data());
    OBL_CHECK_EQ(f.object.m_numWalkPoints, 2u);
    f.env.rands = {1, 2, 3};
    f.Run("EQPTHIRE2");
    OBL_CHECK_EQ(f.object.m_NumLilos, 0);
}

OBL_TEST_CASE("building_construction.commodity_slots_and_beer")
{
    Fixture f;
    BuildingDef& def = f.env.definition;
    def.m_NumCommodities = 2;
    def.m_Commodity[0] = 1;
    def.m_Commodity[1] = 2;
    f.env.commodityNames[1] = "Pizza";
    f.env.commodityNames[2] = "beer";
    f.env.families[1] = 4;
    f.env.families[2] = 5; // ALCOHOL
    def.m_CommodityAB[1][0] = 0.25f;
    def.m_CommodityAB[1][1] = 0.75f;
    def.m_CommodityCost[1] = 6.0f;
    def.m_CommodityF[1] = 0.5f;
    def.m_DefaultBeer = 1;
    f.Run();
    OBL_CHECK(f.object.m_Commod_Intensity[1][0] == 0.25f && f.object.m_Commod_Intensity[1][1] == 0.75f);
    OBL_CHECK(f.object.m_Commod_PriceFactor[0] == 1.0f && f.object.m_Commod_PriceFactor[1] == 1.0f);
    OBL_CHECK_EQ(f.object.m_BeerType, 1);
    OBL_CHECK(f.object.m_BeerStrength == 0.5f);
    // Slot 1 sells beer: BASECOST + STRCOST * 0.5, the beer's PRICE; slot 0 keeps the definition's values.
    OBL_CHECK(f.object.m_Commod_Cost[1] == 3.0f);
    OBL_CHECK(f.object.m_Commod_Price[1] == 3.5f);
    OBL_CHECK(f.object.m_Commod_Price[0] == def.m_CommodityCost[0]);
}

OBL_TEST_CASE("building_construction.beer_not_in_use_takes_the_first_one_that_is")
{
    Fixture f;
    BuildingDef& def = f.env.definition;
    def.m_NumCommodities = 1;
    def.m_Commodity[0] = 1;
    f.env.commodityNames[1] = "Beer";
    f.env.families[1] = 5;
    def.m_DefaultBeer = 0;
    f.env.beers[0].m_scriptFlag = 0;
    f.env.beers[1].m_scriptFlag = 0;
    f.env.beers[2].m_scriptFlag = 1;
    f.Run();
    OBL_CHECK_EQ(f.object.m_BeerType, 2);
    OBL_CHECK(f.object.m_Commod_Cost[0] == 1.0f + 4.0f * 0.5f);
}

OBL_TEST_CASE("building_construction.no_alcohol_no_beer")
{
    Fixture f;
    BuildingDef& def = f.env.definition;
    def.m_NumCommodities = 1;
    def.m_Commodity[0] = 1;
    f.env.commodityNames[1] = "Beer";
    f.env.families[1] = 4;
    f.Run();
    OBL_CHECK_EQ(f.object.m_BeerType, -1);
}
