#pragma once

#include "game/map/grid_cell.h"
#include "render_geometry.h"

#include <cstdint>

namespace obl::map_culling {

struct RectQuery {
    int x, y;
    int srcX, srcY;
    uint32_t width, height;

    bool operator==(const RectQuery&) const = default;
};

RectQuery ObjectQuery(
        int mapX, int mapY, int terrainHeight, int cameraX, int cameraY, int drawOffsetX, int hotspotY, uint32_t width, uint32_t height);

bool DrawObjectsCulls(uint8_t objectType);

bool IsGuestType(uint8_t objectType);

struct WorldPoint {
    float x, y, z;
};

int ScaledViewSize(uint32_t size, float zoomScale);

WorldPoint ViewTopLeft(int cameraX, int cameraY);

WorldPoint ViewBottomRight(int cameraX, int cameraY, int scaledWidth, int scaledHeight);

struct TileRange {
    uint32_t x0, y0;
    uint32_t x1, y1;
};

TileRange FixTileRange(const uint32_t topLeft[2], const uint32_t bottomRight[2]);

inline constexpr uint32_t kSeaMask = 0x60000000u;
int CountSeaTiles(const map::GridCell* cells, uint32_t gridWidth, uint32_t gridHeight, uint32_t x0, uint32_t y0, int x1, int y1);

double SeaShare(int seaCount, const TileRange& range);

} // namespace obl::map_culling
