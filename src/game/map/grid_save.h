#pragma once

#include "game/map/grid_state.h"

#include <cstdint>

// The WorldMap block of the old binary save (the grid save / the grid load), read and written from the native grid. The record list
// exists once here: the world snapshot (EmitGrid) and the grid dump (world_dump.cpp) are visitors over VisitFields.
namespace obl::map {

// The field table: every record of the block after the class name, in save order. `G` is `Grid` or `const Grid`. A visitor provides:
//   Int(name, int32_t field)      an "int" record (m_islandSize)
//   Area(name, Area)              a "uint" record per area of the beach list, then the sea list: 1 for an open area (m_shut false)
//   Dword(name, uint32_t field)   a "dword" record (cell flags)
//   Word(name, uint16_t field)    a "word" record (cell depth offset)
// The cell records follow the area records, one pair per cell in row-major order.
template<class G, class Visitor> void VisitFields(G& grid, Visitor& v)
{
    v.Int("island_size", grid.m_islandSize);
    for (auto& area : grid.m_beachAreas) v.Area("beach_areas_open[]", area);
    for (auto& area : grid.m_seaAreas) v.Area("sea_areas_open[]", area);
    for (uint32_t i = 0, cells = grid.NumCells(); i < cells; ++i) {
        v.Dword("flags[]", grid.m_cells[i].m_flags);
        v.Word("heights[]", grid.m_cells[i].m_zOffset);
    }
}

} // namespace obl::map
