
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/scenery_placement.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using obl::messages::Message;
using obl::object::Building;
using obl::object::Scenery;
using obl::screen::Rect;
using obl::tables::SceneryDef;

namespace {

// Two cells (a position with x < 100 is cell 1, otherwise cell 2) holding the objects standing in them, and a log of what the removal asked for.
struct FakeEnv {
    std::vector<const Scenery*> cells[3];
    std::vector<std::string> log;
    std::vector<obl::object::Building*> buildings;
    std::vector<bool> busy; // HasBuildJob/HasDemolishJob answer per building
    std::vector<Message> posted;

    static int CellIndex(const uint32_t xyz[3]) { return scenery::FloatBits(100.0f) > xyz[0] ? 1 : 2; }
    void* CellAt(const uint32_t xyz[3]) { return &cells[CellIndex(xyz)]; }
    bool CellHas(void* cell, Scenery& s)
    {
        for (const Scenery* o : *static_cast<std::vector<const Scenery*>*>(cell))
            if (o == &s) return true;
        return false;
    }
    void RemoveFromCell(void* cell, Scenery& s)
    {
        auto& list = *static_cast<std::vector<const Scenery*>*>(cell);
        for (size_t i = 0; i < list.size();)
            if (list[i] == &s)
                list.erase(list.begin() + i);
            else
                ++i;
        log.push_back("remove");
    }
    void AddToCell(void* cell, Scenery& s)
    {
        static_cast<std::vector<const Scenery*>*>(cell)->push_back(&s);
        log.push_back("add");
    }
    double TerrainHeight(int x, int y)
    {
        log.push_back("height " + std::to_string(x) + "," + std::to_string(y));
        return 12.75;
    }

    void RestoreBackground(Rect& r) { log.push_back("restore " + Show(r)); }
    void RebuildPathTiles(Rect& r) { log.push_back("paths " + Show(r)); }
    uint32_t NumBuildings() { return static_cast<uint32_t>(buildings.size()); }
    obl::object::Building* Building(uint32_t i) { return buildings[i]; }
    bool HasDemolishJob(obl::object::Building& b) { return busy[Index(b)]; }
    bool HasBuildJob(obl::object::Building&) { return false; }
    const Rect* BuildingRect(obl::object::Building& b)
    {
        log.push_back("rect " + std::to_string(Index(b)));
        return &rects[Index(b)];
    }
    bool Intersect(const Rect& a, const Rect& b, Rect& out)
    {
        if (a.right < b.left || b.right < a.left) return false;
        out = Rect{b.left > a.left ? b.left : a.left, a.top, a.right < b.right ? a.right : b.right, a.bottom};
        return true;
    }
    void BakeBuilding(obl::object::Building& b, const Rect& clip, const float pos[3], int w, int h)
    {
        log.push_back("bake " + std::to_string(Index(b)) + " " + Show(clip) + " " + std::to_string(static_cast<int>(pos[0])) + " " +
                std::to_string(w) + "x" + std::to_string(h));
    }
    void Post(const Message& m)
    {
        posted.push_back(m);
        log.push_back("post");
    }

    std::vector<Rect> rects;
    size_t Index(obl::object::Building& b)
    {
        for (size_t i = 0; i < buildings.size(); ++i)
            if (buildings[i] == &b) return i;
        return 0;
    }
    static std::string Show(const Rect& r)
    {
        return std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom);
    }
};

std::unique_ptr<Scenery> MakeScenery(bool burn, SceneryDef& def)
{
    auto s = std::make_unique<Scenery>();
    obl::unit::FillObject<Scenery>(*s);
    std::memset(&def, 0, sizeof(def));
    def.m_burn = burn ? 1 : 0;
    s->m_definition = &def;
    s->m_id = 77;
    return s;
}

} // namespace

OBL_TEST_CASE("scenery_placement.set_position_moves_between_cells")
{
    FakeEnv env;
    SceneryDef def;
    auto s = MakeScenery(false, def);
    s->m_WorldXYZ[0] = 10.0f;
    s->m_WorldXYZ[1] = 20.0f;
    env.cells[1].push_back(s.get());
    const float target[3] = {150.5f, 33.9f, -4.0f};
    scenery::SetPosition(env, *s, target);
    OBL_CHECK(env.cells[1].empty());
    OBL_REQUIRE(env.cells[2].size() == 1);
    OBL_CHECK(env.cells[2][0] == s.get());
    OBL_CHECK_EQ(s->m_WorldXYZ[0], 150.5f);
    OBL_CHECK_EQ(s->m_WorldXYZ[1], 33.9f);
    OBL_CHECK_EQ(s->m_WorldXYZ[2], 12.75f); // the terrain height replaces z
    OBL_CHECK_EQ(env.log.back(), std::string("height 150,33")); // x and y truncated
}

OBL_TEST_CASE("scenery_placement.set_position_in_the_same_cell_keeps_the_lists")
{
    FakeEnv env;
    SceneryDef def;
    auto s = MakeScenery(false, def);
    s->m_WorldXYZ[0] = 10.0f;
    env.cells[1].push_back(s.get());
    const float target[3] = {50.0f, 1.0f, 0.0f};
    scenery::SetPosition(env, *s, target);
    OBL_CHECK_EQ(env.cells[1].size(), static_cast<size_t>(1));
    OBL_CHECK(env.log.size() == 1); // only the height lookup: no removal, no add
    OBL_CHECK_EQ(s->m_WorldXYZ[0], 50.0f);
}

OBL_TEST_CASE("scenery_placement.set_position_of_an_unlisted_object_joins_the_new_cell")
{
    FakeEnv env;
    SceneryDef def;
    auto s = MakeScenery(false, def);
    s->m_WorldXYZ[0] = 10.0f;
    const float target[3] = {500.0f, 0.0f, 0.0f};
    scenery::SetPosition(env, *s, target);
    OBL_CHECK(env.cells[1].empty());
    OBL_CHECK_EQ(env.cells[2].size(), static_cast<size_t>(1));
}

OBL_TEST_CASE("scenery_placement.removal_of_plain_scenery_only_posts")
{
    FakeEnv env;
    SceneryDef def;
    auto s = MakeScenery(false, def);
    s->m_ScrXYRect = Rect{-5, -5, 9000, 9000};
    scenery::RequestRemoval(env, *s);
    OBL_REQUIRE(env.posted.size() == 1);
    OBL_CHECK_EQ(env.log.size(), static_cast<size_t>(1));
    const Message& m = env.posted[0];
    OBL_CHECK_EQ(m.sender, -3);
    OBL_CHECK_EQ(m.receiver, 77);
    OBL_CHECK_EQ(m.type, static_cast<int>(messages::MT_STATECHANGE));
    OBL_CHECK_EQ(m.flags, 0);
    OBL_CHECK_EQ(m.data, 3);
    OBL_CHECK_EQ(s->m_ScrXYRect.left, -5); // untouched without BURN
}

OBL_TEST_CASE("scenery_placement.removal_of_burned_scenery_restores_and_rebakes")
{
    FakeEnv env;
    SceneryDef def;
    auto s = MakeScenery(true, def);
    s->m_ScrXYRect = Rect{-5, -7, 9000, 120};
    tables::BuildingDef defA{}, defB{}, defC{};
    defA.m_GridWidth = 3;
    defA.m_GridHeight = 4;
    defB.m_GridWidth = 5;
    defB.m_GridHeight = 6;
    defC.m_GridWidth = 7;
    defC.m_GridHeight = 8;
    Building a, b, c;
    obl::unit::FillObject(a);
    obl::unit::FillObject(b);
    obl::unit::FillObject(c);
    a.m_definition = &defA;
    b.m_definition = &defB;
    c.m_definition = &defC;
    a.m_WorldXYZ[0] = 11.0f;
    b.m_WorldXYZ[0] = 22.0f;
    c.m_WorldXYZ[0] = 33.0f;
    env.buildings = {&a, &b, &c};
    env.busy = {false, true, false}; // b is being demolished: skipped before its rectangle is even asked for
    env.rects = {
            Rect{100, 10, 200, 50}, Rect{0, 0, 1, 1}, Rect{5000, 0, 6000, 1}}; // c lies right of the area but inside 0x1000.. no overlap
    scenery::RequestRemoval(env, *s);
    OBL_CHECK_EQ(s->m_ScrXYRect.left, 0);
    OBL_CHECK_EQ(s->m_ScrXYRect.top, 0);
    OBL_CHECK_EQ(s->m_ScrXYRect.right, 0);
    OBL_CHECK_EQ(s->m_ScrXYRect.bottom, 0);
    const std::vector<std::string> expected = {
            "restore 0,0,4096,120", "paths 0,0,4096,120", "rect 0", "bake 0 100,0,200,120 11 3x4", "rect 2", "post"};
    OBL_CHECK(env.log == expected);
}
