#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace obl::map {

enum class ObjectId : uint32_t {};

struct GridCell {
    uint32_t m_flags = 0; // terrain and occupation bits (docs/architecture/map.md)
    std::vector<ObjectId> m_objects;
    uint16_t m_zOffset = 0; // depth offset (the "layer 2" numbers 0x32...0x578 of ApplyFootprint)
};

} // namespace obl::map

namespace obl::grid_cell {
using Cell = map::GridCell;
using map::ObjectId;

inline bool ContainsObject(const Cell& cell, ObjectId object)
{
    return std::find(cell.m_objects.begin(), cell.m_objects.end(), object) != cell.m_objects.end();
}
inline void AddObject(Cell& cell, ObjectId object)
{
    cell.m_objects.push_back(object);
}
// std::list::remove removes every duplicate, preserving the order of survivors.
inline void RemoveObject(Cell& cell, ObjectId object)
{
    std::erase(cell.m_objects, object);
}
} // namespace obl::grid_cell
