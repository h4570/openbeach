#pragma once

#include "construction_queries.h"
#include "core/float_to_int.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// Work points of the construction sites (docs/specs/construction.md): the cells of a building's footprint where builders stand
// (CollectWorkPoints, written once when a job starts), the claim of one of them for a builder (ClaimWorkPoint, GetJobPosition) and the
// entry point the builder script asks for (BuildEntryPoint). The lookups of game state (building definition, grid geometry) go
// through an Env so the logic is unit-tested on its own.
// A missing object or a missing point (a job without work points, a building that does not exist) gives the neutral value (tile 0,0 /
// nothing written); play never reaches those paths.
namespace obl::construction {

constexpr float kEntryEpsilon = 1.1920928955078125e-07f;

struct WorkPointTile {
    int x = 0;
    int y = 0;
};

struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Below test: true when a < b or the operands are unordered (a NaN counts as below).
inline bool LessOrUnordered(float a, float b)
{
    return a < b || std::isnan(a) || std::isnan(b);
}

// ClaimWorkPoint (returns the out pointer): walks the work-point slots while the slot
// number (as a float) is below the building's `m_MaxBuilders`, takes the first slot already owned by `builderToken` or else the first free
// one (owner -1, which then becomes `builderToken`) and answers the tile of its point. No slot: (0,0). The count has no bound but the
// float, the search stops at the ten slots a job has.
// Env: bool MaxBuilders(int buildingId, float* limit)   the building's definition m_MaxBuilders; false = the building does not exist
template<class Env> WorkPointTile ClaimWorkPoint(Job& job, int builderToken, Env& env)
{
    float limit = 0.0f;
    if (!env.MaxBuilders(job.m_buildingId, &limit)) return {};
    for (int slot = 0; slot < 10 && LessOrUnordered(static_cast<float>(slot), limit); ++slot) {
        int& owner = job.m_workPointOwners[slot];
        if (owner != builderToken) {
            if (owner != -1) continue;
            owner = builderToken;
        }
        if (!job.hasWorkPoints) return {};
        const WorkPoint& point = job.workPoints[slot];
        return {static_cast<int>(point.x), static_cast<int>(point.y)};
    }
    return {};
}

// construction::GetJobPosition (returns the out pointer): the claimed point of the building's job as
// floats, z = 0. No job: (0, 0, 0).
template<class Env> Position GetJobPosition(const Manager& manager, int buildingId, int builderToken, Env& env)
{
    Position out;
    if (Job* job = FindJob(manager, buildingId)) {
        const WorkPointTile tile = ClaimWorkPoint(*job, builderToken, env);
        out.x = static_cast<float>(tile.x);
        out.y = static_cast<float>(tile.y);
    }
    return out;
}

// What BuildEntryPoint reads of the building.
struct EntryBuilding {
    bool exists = false;
    bool hasFootprint = false; // BuildingDef::m_footprint != null
    int tileX = 0; // object::Base::GetTilePosition
    int tileY = 0;
};

// The build entry point (outPos, buildingId, builderToken, fallbackX, fallbackY, fallbackZ; returns outPos): the
// point a builder walks to for the building (script values BuildEntPointX/Y). A building with a footprint answers its claimed work point
// (GetJobPosition on the world's manager), replaced by the fallback when both coordinates are within FLT_EPSILON of zero (or not a number).
// A building without one hits a bug kept on purpose: the helper stores the two tile *ints* in its local and hands their bits out as floats
// (tile 40 becomes the denormal 5.6e-44, i.e. practically 0) and its third component is 0.
// Env: EntryBuilding Building(int id)               the object and its definition (a missing building: null)
//      Position JobPosition(int id, int token)      construction::GetJobPosition on the world's manager
template<class Env> Position BuildEntryPoint(int buildingId, int builderToken, const Position& fallback, Env& env)
{
    const EntryBuilding building = env.Building(buildingId);
    if (!building.exists) return {};
    if (!building.hasFootprint) {
        Position out;
        std::memcpy(&out.x, &building.tileX, sizeof(float));
        std::memcpy(&out.y, &building.tileY, sizeof(float));
        return out;
    }
    const Position job = env.JobPosition(buildingId, builderToken);
    // "not greater" includes the unordered case, so a NaN coordinate counts as zero.
    if (!(std::fabs(job.x) > kEntryEpsilon) && !(std::fabs(job.y) > kEntryEpsilon)) return fallback;
    return job;
}

// ---- CollectWorkPoints --------------------------------------------------------------------------------------------------------------------

constexpr int kFootprintStride = 25; // cells per footprint row
constexpr uint32_t kCellUnset = 0x80000000u; // the "free land" marker alone: no work point
constexpr uint32_t kCellBlocked = 0x40000800u; // free-water / "built" marks: no work point

struct TileXY {
    uint32_t x = 0;
    uint32_t y = 0;
};

// What construction::CollectWorkPoints reads of the building.
struct CollectBuilding {
    bool exists = false;
    int tileX = 0, tileY = 0; // object::Base::GetTilePosition
    uint32_t rotation = 0;
    int gridWidth = 0, gridHeight = 0; // BuildingDef::m_GridWidth / m_GridHeight
    const int32_t* workLayer = nullptr;
};

// construction::CollectWorkPoints (from StartBuild/StartDemolish for buildings with a footprint): walks the
// footprint area `(w+2) x (h+2)` from the building's tile in the order Grid::OrientFootprintTraversal gives for its rotation and stores, for
// every cell of footprint layer 1 that holds a direction (anything but 0, the bare 0x80000000 and cells carrying 0x40000800), the cell's world
// position (truncated to ints) and the direction without the 0x80000000 flag in the next work point of the job. With an odd rotation the
// area is turned (columns and rows swap) and the cell of row r, column c is read at c*25 + r instead of r*25 + c.
// The number of points has no bound either; what would go past the job's ten slots is ignored.
// Env: bool Building(int id, CollectBuilding* out)
//      void Orient(TileXY* origin, uint32_t rows, uint32_t columns, int* columnDirection, int* rowDirection, uint32_t rotation)
//                                               Grid::OrientFootprintTraversal
//      TileXY Neighbour(TileXY tile, int direction)   Grid::GetNeighbour
//      void TileToWorld(TileXY tile, float* xy)  Grid::TileToWorldPoint(kind 4): xy[0], xy[1]
template<class Env> void CollectWorkPoints(const Manager& manager, int buildingId, Env& env)
{
    Job* job = FindJob(manager, buildingId);
    CollectBuilding building;
    if (!job || !env.Building(buildingId, &building) || !building.exists || !building.workLayer) return;
    const uint32_t width = static_cast<uint32_t>(building.gridWidth) + 2;
    const uint32_t height = static_cast<uint32_t>(building.gridHeight) + 2;
    const bool turned = (building.rotation & 1) != 0;
    const uint32_t columns = turned ? height : width;
    const uint32_t rows = turned ? width : height;

    TileXY origin{static_cast<uint32_t>(building.tileX), static_cast<uint32_t>(building.tileY)};
    int columnDirection = 0, rowDirection = 0;
    env.Orient(&origin, rows, columns, &columnDirection, &rowDirection, building.rotation);
    TileXY tile = origin;
    int count = 0;
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            const uint32_t a = turned ? column : row;
            const uint32_t b = turned ? row : column;
            const uint32_t cell = static_cast<uint32_t>(building.workLayer[b + kFootprintStride * a]);
            if (cell != kCellUnset && cell != 0 && (cell & kCellBlocked) == 0) {
                float world[2];
                env.TileToWorld(tile, world);
                if (count < kWorkPoints && job->hasWorkPoints) {
                    WorkPoint& point = job->workPoints[count];
                    point.x = static_cast<uint32_t>(FloatToIntWrap(world[0]));
                    point.y = static_cast<uint32_t>(FloatToIntWrap(world[1]));
                    point.direction = static_cast<int>(cell & 0x7fffffffu);
                }
                ++count;
            }
            tile = env.Neighbour(tile, columnDirection);
        }
        tile = env.Neighbour(origin, rowDirection);
        origin = tile;
    }
}

} // namespace obl::construction
