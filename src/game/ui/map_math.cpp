#include "game/ui/map_math.h"

#include <cmath>

namespace obl::map_math {
namespace {

using render_geometry::FloatToIntWrap;

// Constants the game keeps in the data section: (a float, 2^-12) and (a float, 255.0) and (a double, 3.0).
constexpr double kDepthScale = 1.0 / 4096.0;
constexpr double kMaxSample = 255.0;
constexpr double kSearchTolerance = 3.0;
constexpr int kSearchSteps = 100;

uint32_t Wrap(int value)
{
    return static_cast<uint32_t>(value);
}

} // namespace

double TerrainHeight(const Terrain& terrain, int x, int y)
{
    if (x < 0 || x >= terrain.mapWidth || y < 0 || y >= terrain.mapHeight) return 0.0;
    const HeightMap* map = terrain.heightMap;
    if (!map) return 0.0;
    const int width = map->width, height = map->height;
    const int cellWidth = width != 0 ? terrain.mapWidth / width : 0;
    const int cellHeight = height != 0 ? terrain.mapHeight / height : 0;
    if (cellWidth <= 0 || cellHeight <= 0) return 0.0;

    // Fractions within the cell come from the unclamped position; the cell itself is clamped so +1 stays in the image.
    const double fractionX = static_cast<double>(x % cellWidth) / cellWidth;
    const double fractionY = static_cast<double>(y % cellHeight) / cellHeight;
    int column = x / cellWidth;
    if (column > width - 2) column = width - 2;
    int row = y / cellHeight;
    if (row > height - 2) row = height - 2;

    const uint8_t* upper = map->samples + row * width + column;
    const uint8_t* lower = map->samples + (row + 1) * width + column;
    const int upperLeft = upper[0], upperRight = upper[1];
    const int lowerLeft = lower[0], lowerRight = lower[1];
    const double top = static_cast<double>(upperRight - upperLeft) * fractionX + upperLeft;
    const double bottom = static_cast<double>(lowerRight - lowerLeft) * fractionX + lowerLeft;
    return ((bottom - top) * fractionY + top) * static_cast<double>(terrain.heightScale);
}

double TerrainHeightAtWorld(const Terrain& terrain, int worldX, int worldY)
{
    // The Y argument goes through a float on the way: only values above 2^24 lose their low bits.
    const int mapY = FloatToIntWrap(static_cast<double>(static_cast<float>(worldY)) * render_geometry::kWorldYScale);
    if (worldX < 0 || mapY < 0) return 0.0;
    return TerrainHeight(terrain, worldX, mapY);
}

uint32_t Depth(const Terrain& terrain, int planeOffset, int slant, const float* world)
{
    const int mapX = render_geometry::WorldToMapX(world[0]);
    const int mapY = render_geometry::WorldToMapY(world[1]);
    if (mapY < 0 || mapY >= 0x1000) return Wrap(mapY) & 0xffff0000u;
    const int lifted = mapY + FloatToIntWrap(TerrainHeight(terrain, mapX, mapY));
    const int slanted = FloatToIntWrap(static_cast<double>(lifted) * kDepthScale * slant);
    return Wrap(slanted) + Wrap(planeOffset) + (Wrap(lifted) << 4);
}

MapPoint WorldToInt(const float* world)
{
    return {render_geometry::WorldToMapX(world[0]), render_geometry::WorldToMapY(world[1])};
}

ScreenPixel TileToScreen(const Terrain& terrain, int tileSize, uint32_t tileX, uint32_t tileY)
{
    const uint32_t half = static_cast<uint32_t>(tileSize) >> 1;
    const uint32_t quarter = static_cast<uint32_t>(tileSize) >> 2;
    const uint32_t x = tileX * half;
    // (tileX & 1) - 1 is 0 for odd columns and -1 for even ones: the corner of an even column sits a quarter tile up.
    const uint32_t y = ((tileX & 1u) - 1u) * quarter + tileY * half;
    const int height = FloatToIntWrap(TerrainHeight(terrain, static_cast<int>(x), static_cast<int>(y)));
    return {static_cast<int>(x), static_cast<int>(y - Wrap(height))};
}

void ScreenToWorldPoint(const Terrain& terrain, int mapX, int mapY, float out[3])
{
    // Both the interval start and its step are floats in the game (stored between the double operations).
    const int top = FloatToIntWrap(static_cast<double>(terrain.heightScale) * kMaxSample);
    float lift = static_cast<float>(static_cast<double>(top) * 0.5);
    float step = lift;
    for (int i = 0; i < kSearchSteps; ++i) {
        const double height = TerrainHeight(terrain, mapX, mapY + FloatToIntWrap(lift));
        const double distance = std::fabs(height - static_cast<double>(lift));
        // The game leaves the loop when "below or equal" holds, which a NaN also satisfies.
        if (!(distance > kSearchTolerance)) break;
        step = static_cast<float>(static_cast<double>(step) * 0.5);
        // Same: a NaN height takes the "below or equal" branch.
        if (!(height > static_cast<double>(lift)))
            lift = static_cast<float>(static_cast<double>(lift) - step);
        else
            lift = static_cast<float>(static_cast<double>(step) + lift);
    }
    out[0] = static_cast<float>(mapX);
    out[1] = static_cast<float>(2.0 * (static_cast<double>(mapY) + lift));
    out[2] = 0.0f;
}

} // namespace obl::map_math
