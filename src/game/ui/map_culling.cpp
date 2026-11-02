#include "game/ui/map_culling.h"

namespace obl::map_culling {

RectQuery ObjectQuery(
        int mapX, int mapY, int terrainHeight, int cameraX, int cameraY, int drawOffsetX, int hotspotY, uint32_t width, uint32_t height)
{
    auto wrap = [](uint32_t value) { return static_cast<int>(value); };
    const uint32_t screenX = static_cast<uint32_t>(mapX) - static_cast<uint32_t>(cameraX);
    const uint32_t screenY = static_cast<uint32_t>(mapY) - static_cast<uint32_t>(terrainHeight) - static_cast<uint32_t>(cameraY);
    RectQuery query;
    query.x = wrap(static_cast<uint32_t>(drawOffsetX) + screenX);
    query.y = wrap(screenY + static_cast<uint32_t>(hotspotY));
    query.srcX = 0;
    query.srcY = 0;
    query.width = width;
    query.height = height;
    return query;
}

bool DrawObjectsCulls(uint8_t objectType)
{
    return objectType != 1 && objectType != 2;
}

bool IsGuestType(uint8_t objectType)
{
    return objectType == 3;
}

int ScaledViewSize(uint32_t size, float zoomScale)
{
    return render_geometry::FloatToIntWrap(static_cast<double>(size) / static_cast<double>(zoomScale));
}

WorldPoint ViewTopLeft(int cameraX, int cameraY)
{
    return {static_cast<float>(cameraX), static_cast<float>(2.0 * static_cast<double>(cameraY)), 0.0f};
}

WorldPoint ViewBottomRight(int cameraX, int cameraY, int scaledWidth, int scaledHeight)
{
    const int lastX = static_cast<int>(static_cast<uint32_t>(scaledWidth) - 1u);
    const int lastY = static_cast<int>(static_cast<uint32_t>(cameraY) + static_cast<uint32_t>(scaledHeight) - 1u);
    return {static_cast<float>(static_cast<double>(lastX) + static_cast<double>(cameraX)),
            static_cast<float>(2.0 * static_cast<double>(lastY)), 0.0f};
}

TileRange FixTileRange(const uint32_t topLeft[2], const uint32_t bottomRight[2])
{
    TileRange range;
    range.x0 = topLeft[0] == 0x80 ? 0 : topLeft[0];
    range.y0 = topLeft[1] == 0x80 ? 0 : topLeft[1];
    range.x1 = bottomRight[0] == 0 ? 0x80 : bottomRight[0];
    range.y1 = bottomRight[1] == 0 ? 0x80 : bottomRight[1];
    return range;
}

int CountSeaTiles(const map::GridCell* cells, uint32_t gridWidth, uint32_t gridHeight, uint32_t x0, uint32_t y0, int x1, int y1)
{
    if (y1 <= static_cast<int>(y0) || gridWidth == 0 || gridHeight == 0) return 0;
    int count = 0;
    for (uint32_t row = y0; static_cast<int>(row) < y1; ++row) {
        if (static_cast<int>(x0) >= x1) continue;
        const uint32_t rowBase = (row % gridHeight) * gridWidth;
        uint32_t column = x0;
        do {
            if (cells[column % gridWidth + rowBase].m_flags & kSeaMask) ++count;
            ++column;
        } while (static_cast<int>(column) < x1);
    }
    return count;
}

double SeaShare(int seaCount, const TileRange& range)
{
    if (seaCount <= 0) return 0.0;
    const int area = static_cast<int>(static_cast<uint32_t>(range.y1 - range.y0) * static_cast<uint32_t>(range.x1 - range.x0));
    return static_cast<double>(seaCount) / static_cast<double>(area);
}

} // namespace obl::map_culling
