#pragma once

#include "game/map/grid_cell.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"

#include <cstddef>
#include <type_traits>

namespace obl::cell_objects {

inline map::ObjectId IdOf(const object::Base& object)
{
    return static_cast<map::ObjectId>(object.m_id);
}
inline object::Base* ObjectOf(const object::Manager& manager, map::ObjectId id)
{
    return manager.Resolve(static_cast<uint32_t>(id));
}

// The object list of a cell, by object.
inline void AddObject(map::GridCell& cell, const object::Base& object)
{
    grid_cell::AddObject(cell, IdOf(object));
}
inline void RemoveObject(map::GridCell& cell, const object::Base& object)
{
    grid_cell::RemoveObject(cell, IdOf(object));
}
inline bool ContainsObject(const map::GridCell& cell, const object::Base& object)
{
    return grid_cell::ContainsObject(cell, IdOf(object));
}

// Visits the objects by index, so a visitor that adds to or removes from the list cannot invalidate the walk. An id the manager does
// not know (an object destroyed without leaving its cells; never seen in the replays or the chaos runs) is skipped. A visitor that
// returns bool stops the walk with `true`; ForEach then answers true.
template<class Visit> bool ForEach(const object::Manager& manager, const map::GridCell& cell, Visit&& visit)
{
    for (size_t i = 0; i < cell.m_objects.size(); ++i) {
        object::Base* object = ObjectOf(manager, cell.m_objects[i]);
        if (!object) continue;
        if constexpr (std::is_same_v<decltype(visit(object)), bool>) {
            if (visit(object)) return true;
        } else {
            visit(object);
        }
    }
    return false;
}

} // namespace obl::cell_objects
