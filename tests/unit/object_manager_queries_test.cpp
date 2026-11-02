// Side-effect-free ObjectManager queries (src/game/objects/object_manager_queries.h, docs/architecture/objects.md).

#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/ui/sprite.h"
#include "unit_test.h"
#include "object_storage.h"

#include "game/objects/object_manager_queries.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

using namespace obl;
using object::ObjectType;
namespace om = obl::object_manager;

namespace {

struct World {
    std::unique_ptr<object::Manager> manager = std::make_unique<object::Manager>();
    std::vector<std::unique_ptr<object::Base>> objects;

    object::Base* Make(ObjectType type, uint32_t id, const char* name)
    {
        auto o = std::make_unique<object::Base>();
        obl::unit::FillObject<object::Base>(*o);
        std::snprintf(o->m_Name, sizeof(o->m_Name), "%s", name);
        o->m_id = id;
        o->m_type = type;
        objects.push_back(std::move(o));
        return objects.back().get();
    }
    object::Base* AddBuilding(uint32_t id, const char* name)
    {
        object::Base* object = Make(ObjectType::Building, id, name);
        manager->AddBuilding(object, id);
        return object;
    }
    object::Base* AddGuest(uint32_t id, const char* name)
    {
        object::Base* object = Make(ObjectType::Guest, id, name);
        manager->AddGuest(object, id);
        return object;
    }
    object::Base* AddOther(ObjectType type, uint32_t id, const char* name)
    {
        object::Base* object = Make(type, id, name);
        manager->AddOther(object, id);
        return object;
    }
    // A building with a catalog definition of `behavior` (Building::m_definition is what the behavior query reads).
    object::Base* AddBehaviorBuilding(uint32_t id, const char* name, BuildingBehavior behavior)
    {
        auto def = std::make_unique<tables::BuildingDef>();
        def->m_Behavior = behavior;
        auto building = std::make_unique<object::Building>();
        obl::unit::FillObject(*building);
        std::snprintf(building->m_Name, sizeof(building->m_Name), "%s", name);
        building->m_id = id;
        building->m_type = ObjectType::Building;
        building->m_definition = def.get();
        defs.push_back(std::move(def));
        object::Base* object = building.get();
        buildings.push_back(std::move(building));
        manager->AddBuilding(object, id);
        return object;
    }
    std::vector<std::unique_ptr<tables::BuildingDef>> defs;
    std::vector<std::unique_ptr<object::Building>> buildings;
};

} // namespace

OBL_TEST_CASE("object_manager.behavior_query_keeps_the_list_order_of_the_buildings")
{
    World w;
    w.AddBehaviorBuilding(2, "HOTEL", BuildingBehavior::Hotel);
    w.AddBehaviorBuilding(4, "ACCOMBLK", BuildingBehavior::AccomBlock);
    w.AddBehaviorBuilding(5, "HOTEL", BuildingBehavior::Hotel);
    w.AddGuest(6, "HOTEL"); // a guest of the same name is no building and no hotel
    const auto list = om::FindBuildings(*w.manager, BuildingBehavior::Hotel);
    OBL_REQUIRE(list.size() == 2u);
    OBL_CHECK_EQ(list[0]->m_id, 2u);
    OBL_CHECK_EQ(list[1]->m_id, 5u);
    OBL_CHECK_EQ(om::CountBuildingsByBehavior(*w.manager, BuildingBehavior::AccomBlock), 1u);
    OBL_CHECK_EQ(om::CountBuildingsByBehavior(*w.manager, BuildingBehavior::None), 0u);
    OBL_CHECK_EQ(om::CountBuildingsByBehavior(*w.manager, BuildingBehavior::Hotel), 2u);
}

OBL_TEST_CASE("object_manager.get_object_searches_the_three_id_lists")
{
    World w;
    object::Base* b = w.AddBuilding(10, "Bar");
    object::Base* g = w.AddGuest(11, "Guest");
    object::Base* s = w.AddOther(ObjectType::Lifeguard, 12, "Guard");
    const object::Manager& m = *w.manager;
    OBL_CHECK(om::GetObject(m, 10) == b);
    OBL_CHECK(om::GetObject(m, 11) == g);
    OBL_CHECK(om::GetObject(m, 12) == s);
    OBL_CHECK(om::GetObject(m, 13) == nullptr);
    OBL_CHECK(om::GetObject(m, 0xffffffffu) == nullptr);
    OBL_CHECK_EQ(om::FindSlotById(m, 11), om::kGuestBase);
    OBL_CHECK_EQ(om::FindSlotById(m, 12), om::kOtherBase);
    // The id lists are parallel to the objects: an id the manager never made is not found.
    OBL_CHECK(om::GetObject(m, 99) == nullptr);
    const auto saved = m.SavedIds();
    OBL_CHECK_EQ(saved.size(), 18500u);
    OBL_CHECK_EQ(saved[0], 10u);
    OBL_CHECK_EQ(saved[1], 0xffffffffu);
    OBL_CHECK_EQ(saved[om::kGuestBase], 11u);
    OBL_CHECK_EQ(saved[om::kOtherBase], 12u);
}

OBL_TEST_CASE("object_manager.names_compare_without_case_and_return_a_list_of_their_own")
{
    World w;
    object::Base* a = w.AddBuilding(1, "Beach_Bar");
    w.AddGuest(2, "Bob");
    object::Base* c = w.AddOther(ObjectType::Scenery, 3, "BEACH_BAR");
    w.AddOther(ObjectType::Scenery, 4, "Beach_Bar2");
    object::Manager& m = *w.manager;
    OBL_CHECK_EQ(om::CountByName(m, "beach_bar"), 2u);
    const auto found = om::FindByName(m, "beach_BAR");
    OBL_REQUIRE(found.size() == 2u);
    OBL_CHECK(found[0] == a);
    OBL_CHECK(found[1] == c);
    // The result is a copy: the next query does not overwrite it.
    OBL_CHECK_EQ(om::FindByName(m, "Bob").size(), 1u);
    OBL_CHECK(found[0] == a && found[1] == c);
    OBL_CHECK_EQ(om::CountByName(m, ""), 0u);
    OBL_CHECK_EQ(om::CountByName(m, "Bob"), 1u);
}

OBL_TEST_CASE("object_manager.class_lists_copy_buildings_and_guests_and_filter_the_rest")
{
    World w;
    w.AddBuilding(1, "a");
    w.AddBuilding(2, "b");
    w.AddGuest(3, "g");
    object::Base* lg = w.AddOther(ObjectType::Lifeguard, 4, "l");
    w.AddOther(ObjectType::Cleaner, 5, "c");
    object::Base* lg2 = w.AddOther(ObjectType::Lifeguard, 6, "l2");
    object::Manager& m = *w.manager;
    OBL_CHECK(om::ListByClass(m, 2) == m.m_buildings);
    OBL_CHECK(om::ListByClass(m, 3) == m.m_guests);
    OBL_CHECK((om::ListByClass(m, 7) == std::vector<object::Base*>{lg, lg2}));
    OBL_CHECK(om::ListByClass(m, 20).empty());
    OBL_CHECK_EQ(om::CountByClass(m, 7), 2u);
    OBL_CHECK_EQ(om::CountByClass(m, 2), 2u);
    OBL_CHECK_EQ(om::CountByClass(m, 6), 0u);
}

OBL_TEST_CASE("object_manager.staff_without_service_staff_takes_types_7_to_12_from_the_others_only")
{
    World w;
    w.AddGuest(1, "g");
    for (int type = 1; type <= 20; ++type) w.AddOther(static_cast<ObjectType>(type), 100 + type, "o");
    object::Manager& m = *w.manager;
    const auto staff = om::CollectNonServiceStaff(m);
    OBL_REQUIRE(staff.size() == 6u);
    for (uint32_t i = 0; i < 6; ++i) OBL_CHECK_EQ(static_cast<int>(staff[i]->m_type), 7 + static_cast<int>(i));
}

OBL_TEST_CASE("object_manager.near_tile_window_clips_at_zero_and_keeps_the_y_test_quirk")
{
    World w;
    object::Base* guest = w.AddGuest(1, "g");
    object::Base* lifeguard = w.AddOther(ObjectType::Lifeguard, 2, "l");
    object::Base* service = w.AddOther(ObjectType::ServiceStaff, 3, "s");
    object::Base* shark = w.AddOther(ObjectType::Shark, 4, "shark");
    object::Base* vehicle = w.AddOther(ObjectType::Vehicle, 5, "v");
    object::Base* building = w.AddBuilding(6, "b");
    object::Base* table = w.AddOther(ObjectType::Table, 7, "t");
    std::map<std::pair<uint32_t, uint32_t>, std::vector<object::Base*>> tiles;
    tiles[{5, 5}] = {guest, vehicle, building};
    tiles[{6, 5}] = {lifeguard, table};
    tiles[{4, 4}] = {service, shark};
    std::vector<std::pair<uint32_t, uint32_t>> visited;
    auto grid = [&](uint32_t x, uint32_t y, auto&& visit) {
        visited.push_back({x, y});
        auto it = tiles.find({x, y});
        if (it != tiles.end())
            for (object::Base* o : it->second) visit(o);
    };
    // Radius 1 around (5, 5): tiles x 4..6, y 4..6 row by row; vehicles, tables and buildings are not wanted (types 3 and 6..12 only; the shark, 13, is not).
    const auto near = om::ObjectsNearTile(5, 5, 1, grid);
    OBL_CHECK_EQ(near.size(), 3u); // the shark (13) is past the staff range
    OBL_CHECK_EQ(visited.size(), 9u);
    OBL_CHECK((near == std::vector<object::Base*>{service, guest, lifeguard}));
    OBL_CHECK((visited.front() == std::pair<uint32_t, uint32_t>{4, 4}));
    // Clipped at the corner: the window (-1..1) shrinks to 0..1 in both directions.
    visited.clear();
    om::ObjectsNearTile(0, 0, 1, grid);
    OBL_CHECK_EQ(visited.size(), 4u);
    // Radius -1: empty window.
    visited.clear();
    OBL_CHECK(om::ObjectsNearTile(5, 5, -1, grid).empty());
    OBL_CHECK(visited.empty());
    // x beyond 127 is skipped per column; the row test compares x0 - y0 + y: start column 126 makes row offset 2 (y0 + 2) cross 128.
    visited.clear();
    om::ObjectsNearTile(127, 10, 2, grid);
    // columns 125..129, rows 8..12: columns 128 and 129 are cut by the x test; rows are cut once 125 + rowOffset >= 128, i.e. from row offset 3.
    OBL_CHECK_EQ(visited.size(), 3u * 3u);
    // The y that reaches the grid is the real row (wrapping is the grid's job): a window at y 130 still visits it while x0 is small.
    visited.clear();
    om::ObjectsNearTile(3, 130, 0, grid);
    OBL_CHECK_EQ(visited.size(), 1u);
    OBL_CHECK((visited.front() == std::pair<uint32_t, uint32_t>{3, 130}));
}

OBL_TEST_CASE("object_manager.rectangle_keeps_borders_per_class_and_computes_the_far_corner_in_float")
{
    World w;
    auto at = [](object::Base* o, float x, float y) {
        o->m_WorldXYZ[0] = x;
        o->m_WorldXYZ[1] = y;
        return o;
    };
    object::Base* b = at(w.AddBuilding(1, "b"), 2.0f, 2.0f);
    object::Base* g = at(w.AddGuest(2, "g"), 2.0f, 4.0f);
    object::Base* lifeguard = at(w.AddOther(ObjectType::Lifeguard, 3, "l"), 4.0f, 4.0f);
    at(w.AddOther(ObjectType::Mechanic, 4, "m"), 3.0f, 3.0f);
    at(w.AddOther(ObjectType::Lifeguard, 5, "l2"), 4.5f, 3.0f);
    object::Manager& m = *w.manager;
    const float lo[2] = {2.0f, 2.0f};
    const float hi[2] = {4.0f, 4.0f};
    OBL_CHECK((om::ObjectsInRectangle(m, 2, lo, hi) == std::vector<object::Base*>{b}));
    OBL_CHECK((om::ObjectsInRectangle(m, 3, lo, hi) == std::vector<object::Base*>{g}));
    // Other classes also test the type; the border is inclusive.
    OBL_CHECK((om::ObjectsInRectangle(m, static_cast<uint8_t>(ObjectType::Lifeguard), lo, hi) == std::vector<object::Base*>{lifeguard}));
    // max below min: the far corner is min + (max - min) = max, so nothing fits unless the box is a point.
    OBL_CHECK(om::ObjectsInRectangle(m, 2, hi, lo).empty());
    OBL_CHECK_EQ(om::ObjectsInRectangle(m, 2, lo, lo).size(), 1u);
    // compares: a NaN position fails the minimum test, a NaN maximum passes the maximum test.
    b->m_WorldXYZ[0] = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(om::ObjectsInRectangle(m, 2, lo, hi).empty());
    b->m_WorldXYZ[0] = 2.0f;
    const float nanMax[2] = {std::numeric_limits<float>::quiet_NaN(), 4.0f};
    OBL_CHECK_EQ(om::ObjectsInRectangle(m, 2, lo, nanMax).size(), 1u);
}

OBL_TEST_CASE("object_manager.selection_walks_notify_in_array_order")
{
    World w;
    object::Base* b = w.AddBuilding(1, "b");
    object::Base* g = w.AddGuest(2, "g");
    object::Base* service = w.AddOther(ObjectType::ServiceStaff, 3, "s");
    object::Base* lifeguard = w.AddOther(ObjectType::Lifeguard, 4, "l");
    object::Base* security = w.AddOther(ObjectType::SecurityGuard, 5, "x");
    object::Base* shark = w.AddOther(ObjectType::Shark, 6, "sh");
    object::Manager& m = *w.manager;
    std::vector<object::Base*> seen;
    auto record = [&](object::Base* o) { seen.push_back(o); };
    om::ForEachOfClass(m, 2, record);
    OBL_CHECK((seen == std::vector<object::Base*>{b}));
    seen.clear();
    om::ForEachOfClass(m, 3, record);
    OBL_CHECK((seen == std::vector<object::Base*>{g}));
    seen.clear();
    om::ForEachOfClass(m, static_cast<uint8_t>(ObjectType::ServiceStaff), record);
    OBL_CHECK((seen == std::vector<object::Base*>{service}));
    seen.clear();
    om::ForEachObject(m, record);
    OBL_CHECK((seen == std::vector<object::Base*>{b, g, service, lifeguard, security, shark}));
    seen.clear();
    om::ForEachStaff(m, record);
    OBL_CHECK((seen == std::vector<object::Base*>{lifeguard, security}));
}

OBL_TEST_CASE("object_manager.keyline_reaches_guests_and_staff_7_to_12_and_the_manager_flag")
{
    World w;
    std::vector<std::unique_ptr<ui::SpriteInstance>> sprites;
    auto give = [&](object::Base* o) {
        sprites.push_back(std::make_unique<ui::SpriteInstance>());
        std::memset(sprites.back().get(), 0, sizeof(ui::SpriteInstance));
        o->m_sprite = sprites.back().get();
        return sprites.back().get();
    };
    auto* building = give(w.AddBuilding(1, "b"));
    auto* guest = give(w.AddGuest(2, "g"));
    auto* service = give(w.AddOther(ObjectType::ServiceStaff, 3, "s"));
    auto* cleaner = give(w.AddOther(ObjectType::Cleaner, 4, "c"));
    auto* shark = give(w.AddOther(ObjectType::Shark, 5, "sh"));
    object::Manager& m = *w.manager;
    om::SetKeyline(m, 1);
    OBL_CHECK_EQ(guest->m_keyline, 1);
    OBL_CHECK_EQ(cleaner->m_keyline, 1);
    OBL_CHECK_EQ(building->m_keyline, 0);
    OBL_CHECK_EQ(service->m_keyline, 0);
    OBL_CHECK_EQ(shark->m_keyline, 0);
    OBL_CHECK_EQ(m.m_globalKeyline, 1);
}
