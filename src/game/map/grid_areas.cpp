#include "game/map/grid_areas.h"

#include "core/call_counter.h"
#include "core/fatal.h"
#include "game/map/grid_state.h"

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace obl::map {
namespace {

// Calls of the area queries and of the boat route setters (`call-report map.grid-areas`).
test::CallCounter g_areaCalls("map.grid-areas");

void SetShut(Grid& grid, int kind, int area, bool shut, const char* function)
{
    if (!grid_areas::SetAreaShut(grid, kind, area, shut)) {
        char reason[96];
        std::snprintf(reason, sizeof(reason), "an area kind or index out of range (kind %d, area %d)", kind, area);
        fatal::UnhandledInput(function, reason);
    }
    g_areaCalls.Count();
}

int FindArea(const Grid& grid, float x, float y, float z, const std::vector<Area>& areas, const char* function)
{
    int index = 0;
    if (!grid_areas::FindAreaOfPoint(grid, areas, x, y, z, index)) fatal::UnhandledInput(function, "a point outside every area search");
    g_areaCalls.Count();
    return index;
}

} // namespace

bool Grid::IsAreaOpen(int kind, int area) const
{
    bool valid = false;
    const bool open = grid_areas::IsAreaOpen(*this, kind, area, valid);
    if (!valid) fatal::UnhandledInput("Grid::IsAreaOpen", "an area kind or index out of range");
    g_areaCalls.Count();
    return open;
}

void Grid::SetAreaOpen(int kind, int area)
{
    SetShut(*this, kind, area, false, "Grid::SetAreaOpen");
}

void Grid::SetAreaClosed(int kind, int area)
{
    SetShut(*this, kind, area, true, "Grid::SetAreaClosed");
}

int Grid::GetBeachAreaOfPoint(float x, float y, float z) const
{
    return FindArea(*this, x, y, z, m_beachAreas, "Grid::GetBeachAreaOfPoint");
}

int Grid::GetSeaAreaOfPoint(float x, float y, float z) const
{
    return FindArea(*this, x, y, z, m_seaAreas, "Grid::GetSeaAreaOfPoint");
}

void Grid::AddBeachArea(Area area)
{
    g_areaCalls.Count();
    m_beachAreas.push_back(std::move(area));
}

void Grid::AddSeaArea(Area area)
{
    g_areaCalls.Count();
    m_seaAreas.push_back(std::move(area));
}

void Grid::AddBoatPoint(float x, float y, float z, int32_t flags)
{
    g_areaCalls.Count();
    grid_areas::AddBoatPoint(m_boat, x, y, z, flags);
}

void Grid::SetBoatEntry(float x, float y, float z, int32_t number, float dx, float dy)
{
    g_areaCalls.Count();
    grid_areas::SetBoatEnd(m_boat.m_entry, x, y, z, number, dx, dy);
}

void Grid::SetBoatExit(float x, float y, float z, int32_t number, float dx, float dy)
{
    g_areaCalls.Count();
    grid_areas::SetBoatEnd(m_boat.m_exit, x, y, z, number, dx, dy);
}

} // namespace obl::map
