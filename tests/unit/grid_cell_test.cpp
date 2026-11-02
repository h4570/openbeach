#include "unit_test.h"
#include "game/map/cell_objects.h"
#include "game/map/grid_cell.h"
#include <cstring>
#include <string>
#include <vector>

using namespace obl;
namespace {
map::ObjectId H(uint32_t value)
{
    return static_cast<map::ObjectId>(value);
}
} // namespace
OBL_TEST_CASE("grid_cell.append_duplicates_remove_all_keeping_order")
{
    map::GridCell cell;
    OBL_CHECK(cell.m_objects.empty());
    OBL_CHECK_EQ(cell.m_flags | cell.m_zOffset, 0u);
    OBL_CHECK(!grid_cell::ContainsObject(cell, H(0)));
    grid_cell::RemoveObject(cell, H(30));
    for (uint32_t value : {10u, 20u, 10u, 0u, 10u}) grid_cell::AddObject(cell, H(value));
    OBL_CHECK(cell.m_objects == (std::vector<map::ObjectId>{H(10), H(20), H(10), H(0), H(10)}));
    OBL_CHECK(grid_cell::ContainsObject(cell, H(10)));
    OBL_CHECK(grid_cell::ContainsObject(cell, H(0)));
    OBL_CHECK(!grid_cell::ContainsObject(cell, H(30)));
    grid_cell::RemoveObject(cell, H(10));
    OBL_CHECK(cell.m_objects == (std::vector<map::ObjectId>{H(20), H(0)}));
    grid_cell::RemoveObject(cell, H(10));
    grid_cell::RemoveObject(cell, H(0));
    OBL_CHECK(cell.m_objects == (std::vector<map::ObjectId>{H(20)}));
}
OBL_TEST_CASE("cell_objects.ids_resolve_through_the_manager_and_the_walk_survives_removal")
{
    object::Base a{}, b{}, c{}, gone{};
    a.m_id = 7;
    b.m_id = 8;
    c.m_id = 9;
    gone.m_id = 10;
    object::Manager manager;
    manager.AddBuilding(&a, 7);
    manager.AddGuest(&b, 8);
    manager.AddOther(&c, 9);
    map::GridCell cell;
    for (auto* object : {&a, &gone, &b, &c}) grid_cell::AddObject(cell, cell_objects::IdOf(*object));
    OBL_CHECK(cell.m_objects[2] == H(8));
    OBL_CHECK(cell_objects::ObjectOf(manager, cell.m_objects[2]) == &b);
    OBL_CHECK(cell_objects::ObjectOf(manager, H(10)) == nullptr);
    std::vector<const object::Base*> seen;
    // The visitor removes the object it is visiting: the walk by index goes on. An id the manager does not list is skipped.
    cell_objects::ForEach(manager, cell, [&](object::Base* object) {
        seen.push_back(object);
        if (object == &a) grid_cell::RemoveObject(cell, cell_objects::IdOf(a));
    });
    OBL_CHECK(seen == (std::vector<const object::Base*>{&a, &b, &c})); // the list shifted under the walk: index 1 is now b
    int visits = 0;
    OBL_CHECK(cell_objects::ForEach(manager, cell, [&](object::Base* object) { return ++visits, object == &b; }));
    OBL_CHECK_EQ(visits, 1);
    OBL_CHECK(!cell_objects::ForEach(manager, cell, [](object::Base*) { return false; }));
}
