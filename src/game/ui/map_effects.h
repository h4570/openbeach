#pragma once

#include "game/ui/map_math.h"

#include <cstdint>

namespace obl::map_effects {

struct SceneBuffer {
    uint32_t* pixels;
    int width;
    int height;
};

void DarkenByDepth(const SceneBuffer& scene, int cameraY, int planeOffset, int slant, uint16_t halfMask);

struct TileHighlight {
    uint32_t tileSize;
    uint32_t tile[2];
    uint32_t neighbour2[2];
    uint32_t neighbour3[2];
    float origin[3];
    int cameraX, cameraY;
    int zoomLevel;
    float zoomScale;
    int planeOffset, slant;
    uint16_t color;
};

struct HighlightStats {
    int written = 0;
    int rejected = 0;
};
void DrawTileHighlight(
        const SceneBuffer& scene, const map_math::Terrain& terrain, const TileHighlight& highlight, HighlightStats* stats = nullptr);

struct SeaRange {
    int firstColumn, firstRow;
    int endColumn, endRow;
};

SeaRange VisibleSeaBlocks(int cameraX, int cameraY, int viewWidth, int viewHeight, int blockSize, float zoomScale);

struct SeaBlockOrigin {
    int x, y;
};
SeaBlockOrigin BlockOrigin(int blockSize, int column, int row, int cameraX, int cameraY);

} // namespace obl::map_effects
