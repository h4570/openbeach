#include "game/objects/object_registry_api.h"
#include "game/objects/object_hit_test.h"

#include "game/objects/object_static.h"
#include "game/game/game_accessors.h"
#include "game/map/cell_objects.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_queries.h"
#include "game/objects/object_manager_state.h"
#include "core/call_counter.h"

#include <cstdint>

namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;

namespace {

namespace om = obl::object_manager;

// C++ predicates share the same object-only signature.
using PredicateFunc = bool (*)(object::Base*);

// Call counters (`call-report objects.object-manager-queries`).
test::CallCounter g_queries("objects.object-manager-queries");

// The objects on a tile: the cell's object list.
struct GridTiles {
    map::Grid& grid;
    template<class Visit> void operator()(uint32_t x, uint32_t y, Visit&& visit) const
    {
        cell_objects::ForEach(*GetObjectManager(), grid.Cell(x, y), [&](object::Base* object) { visit(object); });
    }
};

// ClearSelection notifies the object that it is no longer selected.
void ClearObjectSelection(object::Base* object)
{
    object->ClearSelection();
}

} // namespace

namespace object_manager {

ClearSelectionProbe g_clearSelectionProbe = nullptr;

object::Base* ObjectById(object::Manager* manager, uint32_t id)
{
    g_queries.Count();
    return om::GetObject(*manager, id);
}

ObjectList FindObjectsByName(object::Manager* manager, const char* name)
{
    g_queries.Count();
    return om::FindByName(*manager, name);
}

int32_t CountByName(object::Manager* manager, const char* name)
{
    g_queries.Count();
    return static_cast<int32_t>(om::CountByName(*manager, name));
}

ObjectList GetListByClass(object::Manager* manager, int objectClass)
{
    g_queries.Count();
    return om::ListByClass(*manager, static_cast<char>(objectClass));
}

ObjectList GetNonServiceStaffList(object::Manager* manager)
{
    g_queries.Count();
    return om::CollectNonServiceStaff(*manager);
}

int32_t CountByClass(object::Manager* manager, int objectClass)
{
    g_queries.Count();
    return static_cast<int32_t>(om::CountByClass(*manager, static_cast<char>(objectClass)));
}

ObjectList GetObjectsNearTile(object::Manager*, int32_t tileX, int32_t tileY, int32_t radius)
{
    g_queries.Count();
    return om::ObjectsNearTile(tileX, tileY, radius, GridTiles{*GetGrid()});
}

ObjectList FilterByCursorPredicate(object::Manager* manager, PredicateFunc predicate)
{
    g_queries.Count();
    auto matches = [&](const object::Base* object) { return predicate(const_cast<object::Base*>(object)); };
    return om::FilterObjects(*manager, matches);
}

ObjectList GetObjectsUnderCursor(object::Manager* manager)
{
    return FilterByCursorPredicate(manager, &object::HitTestCursor);
}

ObjectList GetObjectsUnderCursorUnselectable(object::Manager* manager)
{
    return FilterByCursorPredicate(manager, &object::HitTestCursorUnselectable);
}

ObjectList GetObjectsInRectangle(object::Manager* manager, uint32_t objectClass, const float* minPoint, const float* maxPoint)
{
    g_queries.Count();
    return om::ObjectsInRectangle(*manager, static_cast<uint8_t>(objectClass), minPoint, maxPoint);
}

void ClearSelectionByClass(object::Manager* manager, uint32_t objectClass)
{
    if (g_clearSelectionProbe && g_clearSelectionProbe(manager, objectClass)) return;
    g_queries.Count();
    om::ForEachOfClass(*manager, static_cast<uint8_t>(objectClass), ClearObjectSelection);
}

void NotifyAllObjects(object::Manager* manager)
{
    g_queries.Count();
    om::ForEachObject(*manager, ClearObjectSelection);
}

void ClearStaffSelection(object::Manager* manager)
{
    g_queries.Count();
    om::ForEachStaff(*manager, ClearObjectSelection);
}

void SetKeyline(object::Manager* manager, uint8_t value)
{
    g_queries.Count();
    om::SetKeyline(*manager, value);
}

} // namespace object_manager
} // namespace obl::object
