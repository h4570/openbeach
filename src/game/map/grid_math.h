#pragma once

#include "core/float_to_int.h"

#include <cstdint>

// Pure tile geometry of the terrain grid (docs/architecture/map.md): Neighbour and TileToWorldPoint. The grid is staggered: columns with an even and an odd x have different neighbour offsets. The route search
// calls both for every expanded tile; the renderer and the construction code call them as well.
namespace obl::grid_math {

struct TileXY {
    uint32_t x;
    uint32_t y;
};

inline TileXY Neighbour(uint32_t width, uint32_t height, uint32_t x, uint32_t y, int direction)
{
    uint32_t nx = x, ny = y;
    if ((x & 1) == 0) {
        switch (direction) {
        case 0:
            nx = x + 1;
            ny = y - 1;
            break;
        case 1: nx = x + 2; break;
        case 2: nx = x + 1; break;
        case 3: ny = y + 1; break;
        case 4: nx = x - 1; break;
        case 5: nx = x - 2; break;
        case 6:
            nx = x - 1;
            ny = y - 1;
            break;
        case 7: ny = y - 1; break;
        default: break;
        }
    } else {
        switch (direction) {
        case 0: nx = x + 1; break;
        case 1: nx = x + 2; break;
        case 2:
            nx = x + 1;
            ny = y + 1;
            break;
        case 3: ny = y + 1; break;
        case 4:
            nx = x - 1;
            ny = y + 1;
            break;
        case 5: nx = x - 2; break;
        case 6: nx = x - 1; break;
        case 7: ny = y - 1; break;
        default: break;
        }
    }
    return {nx % width, ny % height};
}

struct WorldPoint {
    float x;
    float y;
    float z;
};

// Which point of the tile TileToWorldPoint returns (the `kind` argument). Anything else answers the tile origin.
enum class TilePoint : int {
    Origin = 0, // also every value outside 1..4
    BottomCorner = 1, // y + (tileSize - 1)
    Left = 2, // x - tileSize/2, y + tileSize/2
    Right = 3, // x + tileSize/2, y + tileSize/2
    Bottom = 4, // y + tileSize/2 (the point the A* measures with)
};

inline WorldPoint TileToWorldPoint(int tileSize, uint32_t tileX, uint32_t tileY, int kind)
{
    const int half = tileSize / 2; // signed: rounds toward zero
    const uint32_t uTile = static_cast<uint32_t>(tileSize);
    const float x = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(half) * tileX));
    int32_t yInt = static_cast<int32_t>(tileY * uTile);
    if ((tileX & 1) == 0) yInt = static_cast<int32_t>(static_cast<uint32_t>(yInt) - static_cast<uint32_t>(half));
    const float yTile = static_cast<float>(yInt);

    const double shift = static_cast<double>(uTile >> 1); // unsigned shift
    switch (kind) {
    case 1: return {x, static_cast<float>(static_cast<double>(uTile - 1) + yTile), 0.0f};
    case 2: return {static_cast<float>(static_cast<double>(x) - shift), static_cast<float>(shift + yTile), 0.0f};
    case 3: return {static_cast<float>(shift + static_cast<double>(x)), static_cast<float>(shift + yTile), 0.0f};
    case 4: return {x, static_cast<float>(shift + yTile), 0.0f};
    default: return {x, yTile, 0.0f};
    }
}

struct PathPoint {
    float x;
    float y;
};

// The point a route step walks to, x = (tileSize >> 1) * tileX and
// y = tileSize * tileY, plus half a tile in the odd columns. Unlike TileToWorldPoint the products are 32-bit unsigned (converted as zero-extended
// 64-bit values), the even columns are not moved up, and the pair is returned by float stores.
inline PathPoint TileToPathPoint(int tileSize, uint32_t tileX, uint32_t tileY)
{
    const uint32_t size = static_cast<uint32_t>(tileSize);
    const uint32_t half = size >> 1;
    const uint32_t x = half * tileX;
    const uint32_t y = size * tileY + (tileX & 1) * half;
    return {static_cast<float>(static_cast<double>(x)), static_cast<float>(static_cast<double>(y))};
}

// Locate the middle of the tile's lower half with one final rounding to float. Adding the half-tile offset in double
// preserves the integer origin's precision, whereas TileToWorldPoint rounds the origin before applying the offset.
inline PathPoint TileToWorldCenter(int tileSize, uint32_t tileX, uint32_t tileY)
{
    const int half = tileSize / 2;
    const uint32_t uTile = static_cast<uint32_t>(tileSize);
    const float x = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(half) * tileX));
    int32_t yInt = static_cast<int32_t>(tileY * uTile);
    if ((tileX & 1) == 0) yInt = static_cast<int32_t>(static_cast<uint32_t>(yInt) - static_cast<uint32_t>(half));
    return {x, static_cast<float>(static_cast<double>(yInt) + static_cast<double>(uTile >> 1))};
}

inline uint32_t TileIndex(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY)
{
    return (tileY % height) * width + tileX % width;
}

inline bool AreTilesAdjacent(uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t otherX, uint32_t otherY)
{
    for (int direction = 0; direction < 8; ++direction) {
        const TileXY n = Neighbour(width, height, x, y, direction);
        if (n.x == otherX && n.y == otherY) return true;
    }
    return false;
}

// The cell indexes of the eight neighbours in direction order.
inline void NeighbourIndices(uint32_t width, uint32_t height, uint32_t x, uint32_t y, uint32_t out[8])
{
    for (int direction = 0; direction < 8; ++direction) {
        const TileXY n = Neighbour(width, height, x, y, direction);
        out[direction] = TileIndex(width, height, n.x, n.y);
    }
}

inline TileXY WorldPointToTile(int tileSize, uint32_t width, uint32_t height, float worldX, float worldY)
{
    const uint32_t half = static_cast<uint32_t>(tileSize) >> 1;
    if (half == 0) return {0, 0};
    const uint32_t px = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(worldX)));
    const uint32_t py = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(worldY)));
    uint32_t qx = px / half;
    uint32_t qy = py / half;
    const uint32_t rx = px % half;
    const uint32_t ry = py % half;
    const bool oddRow = (qy & 1) != 0;
    const bool oddColumn = (qx & 1) != 0;
    if (oddRow == oddColumn) {
        if (rx > half - ry) {
            ++qx;
            if (oddRow) ++qy;
        }
    } else if (oddRow) {
        if (rx > ry)
            ++qx;
        else
            ++qy;
    } else if (rx > ry) {
        ++qx;
    }
    return {qx % width, (qy >> 1) % height};
}

// The footprint queries take their dimensions from the global grid, not from `this`; the caller passes
// the global one here (Orient also reads `this` once, see below).
struct GridDims {
    int tileSize;
    uint32_t width;
    uint32_t height;
};

// One step along the east / west diagonal used by the footprint walks: the row drops by one in an even column before the column
// moves, all in wrapped 32-bit unsigned arithmetic (same remark as Neighbour).
inline void StepEast(const GridDims& g, uint32_t& x, uint32_t& y)
{
    const uint32_t row = (x & 1) == 0 ? y - 1 : y;
    x = (x + 1) % g.width;
    y = row % g.height;
}

inline void StepWest(const GridDims& g, uint32_t& x, uint32_t& y)
{
    const uint32_t row = (x & 1) == 0 ? y - 1 : y;
    x = (x - 1) % g.width;
    y = row % g.height;
}

inline bool FootprintCrossesMapBoundary(const GridDims& g, uint32_t tileX, uint32_t tileY, int width, int height)
{
    uint32_t left = tileX;
    for (int i = width; i > 0; --i) left = (left - 1) % g.width;
    uint32_t x = tileX, y = tileY;
    for (int i = height; i > 0; --i) StepEast(g, x, y);
    uint32_t endX = x, endY = y;
    for (int i = width; i > 0; --i) StepWest(g, endX, endY);
    return !(static_cast<int32_t>(left) <= static_cast<int32_t>(x) && static_cast<int32_t>(endY) <= static_cast<int32_t>(tileY));
}

struct TraversalDirections {
    int columnDirection;
    int rowDirection;
};

// Moves `origin` to the corner of a rows x columns footprint where the walk for `rotation`
// (0..3) starts and gives the Neighbour directions of the column and the row step. The origin is first wrapped (and its row moved
// down by one) with the dimensions of `this` (`own`); the corner walk uses the global grid (`global`). A rotation above 3 only does
// the first step and leaves the directions untouched (`touchedDirections` false).
inline TraversalDirections OrientFootprintTraversal(
        const GridDims& own, const GridDims& global, TileXY& origin, int rows, int columns, uint32_t rotation, bool& touchedDirections)
{
    origin.x %= own.width;
    origin.y = (origin.y + 1) % own.height;
    touchedDirections = true;
    switch (rotation) {
    case 0:
        for (int64_t n = static_cast<int64_t>(rows) - 1; n > 0; --n) StepEast(global, origin.x, origin.y);
        for (int64_t n = static_cast<int64_t>(columns) - 1; n > 0; --n) StepWest(global, origin.x, origin.y);
        return {2, 4};
    case 1:
        for (int64_t n = static_cast<int64_t>(columns) - 1; n > 0; --n)
            origin = Neighbour(global.width, global.height, origin.x, origin.y, 6);
        return {2, 0};
    case 2: return {6, 0};
    case 3:
        for (int64_t n = static_cast<int64_t>(rows) - 1; n > 0; --n) origin = Neighbour(global.width, global.height, origin.x, origin.y, 0);
        return {6, 4};
    default: touchedDirections = false; return {0, 0};
    }
}

inline bool FootprintOffsetPoint(const GridDims& g, uint32_t tileX, uint32_t tileY, int width, int height, float offsetX, float offsetY,
        uint32_t rotation, PathPoint& out)
{
    uint32_t x = tileX, y = tileY;
    switch (rotation) {
    case 0: {
        const WorldPoint o = TileToWorldPoint(g.tileSize, x, y, 0);
        const float cornerY = static_cast<float>(static_cast<double>(static_cast<uint32_t>(g.tileSize) - 1) + o.y);
        out.x = static_cast<float>(static_cast<double>(o.x) + offsetX);
        out.y = static_cast<float>(static_cast<double>(cornerY) + offsetY);
        return true;
    }
    case 1: {
        for (int64_t n = static_cast<int64_t>(width) - 1; n > 0; --n) StepEast(g, x, y);
        const WorldPoint o = TileToWorldPoint(g.tileSize, x, y, 0);
        const double half = static_cast<double>(static_cast<uint32_t>(g.tileSize) >> 1);
        const float px = static_cast<float>(half + o.x);
        const float py = static_cast<float>(half + o.y);
        out.x = static_cast<float>(static_cast<double>(px) + offsetY);
        out.y = static_cast<float>(static_cast<double>(py) - offsetX);
        return true;
    }
    case 2: {
        for (int64_t n = static_cast<int64_t>(height) - 1; n > 0; --n) StepEast(g, x, y);
        for (int64_t n = static_cast<int64_t>(width) - 1; n > 0; --n) {
            const TileXY next = Neighbour(g.width, g.height, x, y, 6);
            x = next.x;
            y = next.y;
        }
        const WorldPoint o = TileToWorldPoint(g.tileSize, x, y, 0);
        out.x = static_cast<float>(static_cast<double>(o.x) - offsetX);
        out.y = static_cast<float>(static_cast<double>(o.y) - offsetY);
        return true;
    }
    case 3: {
        for (int64_t n = static_cast<int64_t>(height) - 1; n > 0; --n) {
            const TileXY next = Neighbour(g.width, g.height, x, y, 6);
            x = next.x;
            y = next.y;
        }
        const WorldPoint o = TileToWorldPoint(g.tileSize, x, y, 2);
        out.x = static_cast<float>(static_cast<double>(o.x) - offsetY);
        out.y = static_cast<float>(static_cast<double>(o.y) + offsetX);
        return true;
    }
    default: return false;
    }
}

inline bool CanPlaceSceneryTile(uint32_t flags, uint32_t mask)
{
    if (mask == 1) return (flags & 0x10000000) == 0 && (flags & 1) != 0 && flags > 0x20000000;
    return (flags & mask) != 0 && flags > 0x20000000 && (flags & 0x10) == 0;
}

inline bool CanPlaceBuildingEdge(uint32_t flags, uint32_t mask)
{
    if ((flags & mask) == 0 && (flags & 0xe0000100u) == 0) return false;
    if ((flags & 0x50) != 0) return false;
    const bool bit0 = (mask & 1) != 0;
    const bool bit1 = (mask & 2) != 0;
    if (bit0 && bit1) return (flags & 3) != 0;
    if (bit0) return (flags & 1) != 0;
    if (bit1) return ((flags >> 1) & 1) != 0;
    return ((~(flags >> 8)) & 1) != 0;
}

} // namespace obl::grid_math
