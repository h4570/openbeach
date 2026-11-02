#pragma once

#include <cstdint>
#include <vector>

namespace obl::object {

struct Base;
} // namespace obl::object
namespace obl::layout {} // namespace obl::layout
namespace obl::object {
struct Manager;
}

namespace obl::object {

// The object_manager:: facades answer the ObjectManager queries with the manager as the first argument.
// The pure logic is in src/game/objects/object_manager_queries.h (namespace obl::object_manager). The list builders return a `std::vector` of their own (a copy of the matching objects, in list order).
namespace object_manager {

using Manager = object::Manager;
using ObjectList = std::vector<object::Base*>;

object::Base* ObjectById(Manager* manager, uint32_t id);
ObjectList FindObjectsByName(Manager* manager, const char* name);
int32_t CountByName(Manager* manager, const char* name);
ObjectList GetListByClass(Manager* manager, int objectClass);
ObjectList GetNonServiceStaffList(Manager* manager);
int32_t CountByClass(Manager* manager, int objectClass);
ObjectList GetObjectsNearTile(Manager* manager, int32_t tileX, int32_t tileY, int32_t radius);
// The hit-test predicates of the cursor queries are the HitTestCursor* functions of the objects.
ObjectList GetObjectsUnderCursor(Manager* manager);
ObjectList GetObjectsUnderCursorUnselectable(Manager* manager);
ObjectList GetObjectsInRectangle(Manager* manager, uint32_t objectClass, const float* minPoint, const float* maxPoint);
void ClearSelectionByClass(Manager* manager, uint32_t objectClass);
void NotifyAllObjects(Manager* manager);
void ClearStaffSelection(Manager* manager);
void SetKeyline(Manager* manager, uint8_t value);

// Test seam of the guest selection: when set, called before ClearSelectionByClass; true swallows the call (no object is told). Null in the
// game.
using ClearSelectionProbe = bool (*)(Manager* manager, uint32_t objectClass);
extern ClearSelectionProbe g_clearSelectionProbe;

} // namespace object_manager
} // namespace obl::object
