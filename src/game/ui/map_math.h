#pragma once

#include "render_geometry.h"

#include <cstdint>

// The coordinate and height arithmetic of MapView (docs/architecture/map-view.md): terrain height
// from the Grid's height map, the depth of a world point, screen <-> world conversions. Pure functions over numbers
// the callers gather from `g_MapView` and `g_Grid`.
//
// Every operation rounds like an IEEE double, and the order of the arithmetic is part of the behavior.
namespace obl::map_math {

// The grid's height map (map::Grid::HeightMap): sample (column, row) is byte `samples[row * width + column]`.
struct HeightMap {
    int width;
    int height;
    const uint8_t* samples;
};

// Everything MapView::GetTerrainHeight reads: the map size in pixels, the grid's height map (null = flat
// terrain) and its scale (height of a full 255 sample).
struct Terrain {
    int mapWidth;
    int mapHeight;
    const HeightMap* heightMap;
    float heightScale;
};

// MapView::GetTerrainHeight: bilinear sample of the height map at map pixel (x, y) times the scale; 0 outside
// the map or without a height map. The result is not rounded to float; callers truncate it with FloatToIntWrap.
// A height map whose cells would be zero pixels wide (width or height above the map size, or below 1) never occurs in
// the game; it yields 0 here where the game would raise a division fault.
double TerrainHeight(const Terrain& terrain, int x, int y);

// MapView::GetTerrainHeightF: `x` is a truncated world X, `y` a truncated world Y (halved here to a map row);
// a negative map column or row gives 0 before the height is looked up.
double TerrainHeightAtWorld(const Terrain& terrain, int worldX, int worldY);

// MapView::GetDepth (a 32-bit value): depth of a world point (floats 0 and 1) for the z-buffer. Outside map rows
// 0..0xfff only the high 16 bits of the row survive: the low 16 bits of the row number are cleared.
// `planeOffset` and `slant` are the map view's plane offset and slant.
uint32_t Depth(const Terrain& terrain, int planeOffset, int slant, const float* world);

// MapView::WorldToInt: truncated map column and row of a world point.
struct MapPoint {
    int x, y;
};
MapPoint WorldToInt(const float* world);

// MapView::TileToScreen: the tile's reference corner in map pixels, lifted by the terrain height. `tileSize` is
// the grid's tile size (world units of a tile edge; halved and quartered as an unsigned number).
struct ScreenPixel {
    int x, y;
};
ScreenPixel TileToScreen(const Terrain& terrain, int tileSize, uint32_t tileX, uint32_t tileY);

// MapView::ScreenToWorldPoint: the world point under a map pixel, found by bisecting the height the terrain
// lifts the pixel by (at most 100 steps, stops within 3.0). out[2] is 0. Safe for no aliasing only.
void ScreenToWorldPoint(const Terrain& terrain, int mapX, int mapY, float out[3]);

} // namespace obl::map_math
