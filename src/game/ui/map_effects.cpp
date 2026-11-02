#include "game/ui/map_effects.h"

#include "render_geometry.h"

#include <cstddef>

namespace obl::map_effects {
namespace {

using render_geometry::FloatToIntWrap;

constexpr double kDepthScale = 1.0 / 4096.0;
constexpr double kRowDepthStep = 16.0;
constexpr uint16_t kHighlightDepthBias = 0x10;

uint32_t Wrap(int value)
{
    return static_cast<uint32_t>(value);
}
int Signed(uint32_t value)
{
    return static_cast<int>(value);
}

uint16_t RowDepth(int row, int planeOffset, int slant, int terrainLift)
{
    if (row < 0 || row > 0xfff) return 0;
    const int lifted = Signed(Wrap(row) + Wrap(terrainLift));
    const int slanted = FloatToIntWrap(static_cast<double>(lifted) * kDepthScale * slant);
    return static_cast<uint16_t>(Wrap(slanted) + Wrap(planeOffset) + (Wrap(lifted) << 4));
}

void FillHighlightRow(const SceneBuffer& scene, int y, int centre, int halfWidth, uint16_t depth, uint16_t color, HighlightStats* stats)
{
    if (y < 0 || y >= scene.height) return;
    const int parity = y & 1;

    int x = Signed((Wrap(centre) - Wrap(halfWidth / 2)) * 2u + (parity == 0 ? 2u : 0u));
    if (parity >= halfWidth) return;
    uint32_t count = Wrap(halfWidth - parity + 1) >> 1;
    uint32_t* row = scene.pixels + static_cast<size_t>(y) * static_cast<size_t>(scene.width);
    const int limit = Signed(Wrap(scene.width) * 2u);
    for (; count != 0; --count, x = Signed(Wrap(x) + 4u)) {
        if (x >= limit || x < 0) continue;
        uint32_t& pixel = row[x / 2];
        const bool pass = (pixel >> 16) <= depth;
        if (pass) pixel = (pixel & 0xffff0000u) | color;
        if (stats) ++(pass ? stats->written : stats->rejected);
    }
}

} // namespace

void DarkenByDepth(const SceneBuffer& scene, int cameraY, int planeOffset, int slant, uint16_t halfMask)
{
    if (scene.width <= 0 || scene.height <= 0) return;
    uint32_t* pixel = scene.pixels;
    for (int row = 0; row < scene.height; ++row) {
        const uint16_t depth = RowDepth(Signed(Wrap(cameraY) + Wrap(row)), planeOffset, slant, 0);
        for (int column = 0; column < scene.width; ++column, ++pixel) {
            const uint32_t value = *pixel;
            if ((value >> 16) <= depth) *pixel = ((static_cast<uint32_t>(halfMask) >> 1) & (value >> 1)) + (value & 0xffff0000u);
        }
    }
}

void DrawTileHighlight(const SceneBuffer& scene, const map_math::Terrain& terrain, const TileHighlight& h, HighlightStats* stats)
{
    const map_math::ScreenPixel top = map_math::TileToScreen(terrain, static_cast<int>(h.tileSize), h.tile[0], h.tile[1]);
    const map_math::ScreenPixel left = map_math::TileToScreen(terrain, static_cast<int>(h.tileSize), h.neighbour2[0], h.neighbour2[1]);
    const map_math::ScreenPixel right = map_math::TileToScreen(terrain, static_cast<int>(h.tileSize), h.neighbour3[0], h.neighbour3[1]);

    const int originX = render_geometry::WorldToMapX(h.origin[0]);
    const int originY = render_geometry::WorldToMapY(h.origin[1]);
    uint16_t depth = 0;
    if (originY >= 0 && originY <= 0xfff)
        depth = RowDepth(originY, h.planeOffset, h.slant, FloatToIntWrap(map_math::TerrainHeight(terrain, originX, originY)));
    depth = static_cast<uint16_t>(depth + kHighlightDepthBias);

    int centre = Signed(Wrap(top.x) - Wrap(h.cameraX));
    int firstRow = Signed(Wrap(top.y) - Wrap(h.cameraY));
    int leftRow = Signed(Wrap(left.y) - Wrap(h.cameraY));
    int rightRow = Signed(Wrap(right.y) - Wrap(h.cameraY));
    float scale = 1.0f;
    if (h.zoomLevel == 2) {
        scale = h.zoomScale;
        const double factor = static_cast<double>(scale);
        centre = FloatToIntWrap(static_cast<double>(centre) * factor);
        firstRow = FloatToIntWrap(static_cast<double>(firstRow) * factor);
        leftRow = FloatToIntWrap(static_cast<double>(leftRow) * factor);
        rightRow = FloatToIntWrap(static_cast<double>(rightRow) * factor);
    }
    const uint16_t step = static_cast<uint16_t>(FloatToIntWrap(kRowDepthStep / static_cast<double>(scale)));

    int y = firstRow;
    int halfWidth = 1;
    for (; y <= leftRow; ++y) {
        FillHighlightRow(scene, y, centre, halfWidth, depth, h.color, stats);
        halfWidth = Signed(Wrap(halfWidth) + 4u);
        depth = static_cast<uint16_t>(depth + step);
    }
    halfWidth = Signed(Wrap(halfWidth) - 8u);
    for (; y <= rightRow; ++y) {
        FillHighlightRow(scene, y, centre, halfWidth, depth, h.color, stats);
        halfWidth = Signed(Wrap(halfWidth) - 4u);
        depth = static_cast<uint16_t>(depth + step);
    }
}

SeaRange VisibleSeaBlocks(int cameraX, int cameraY, int viewWidth, int viewHeight, int blockSize, float zoomScale)
{
    if (blockSize == 0) return {0, 0, 0, 0};

    auto end = [zoomScale](int camera, int size) {
        const double scaled = static_cast<double>(static_cast<uint32_t>(size)) / static_cast<double>(zoomScale);
        const int far = Signed(Wrap(FloatToIntWrap(scaled)) + Wrap(camera));
        return ((far + ((far >> 31) & 0x7f)) >> 7) + 1;
    };

    return {cameraX / blockSize, cameraY / blockSize, end(cameraX, viewWidth), end(cameraY, viewHeight)};
}

SeaBlockOrigin BlockOrigin(int blockSize, int column, int row, int cameraX, int cameraY)
{
    return {Signed(Wrap(blockSize) * Wrap(column) - Wrap(cameraX)), Signed(Wrap(blockSize) * Wrap(row) - Wrap(cameraY))};
}

} // namespace obl::map_effects
