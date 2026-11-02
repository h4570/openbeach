#include "render_geometry.h"

#include <climits>
#include <cmath>

namespace obl::render_geometry {

int FloatToIntWrap(double value)
{
    // Float to int: a truncation to 64 bits whose low word is returned. Values beyond int wrap, and NaN or anything
    // outside int64 gives the 64-bit "integer indefinite" 0x8000000000000000, whose low word is 0. Comparisons are false for NaN.
    if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<int64_t>(value)));
}

int WorldToMapX(float worldX)
{
    return FloatToIntWrap(worldX);
}

int WorldToMapY(float worldY)
{
    return FloatToIntWrap(static_cast<double>(worldY) * kWorldYScale);
}

ScreenPoint MapToScreen(int mapX, int mapY, int terrainHeight, int cameraX, int cameraY)
{
    // Unsigned arithmetic: the game wraps silently on extreme coordinates.
    return {static_cast<int>(static_cast<uint32_t>(mapX) - static_cast<uint32_t>(cameraX)),
            static_cast<int>(static_cast<uint32_t>(mapY) - static_cast<uint32_t>(terrainHeight) - static_cast<uint32_t>(cameraY))};
}

bool UsesFirstAnim(int spriteType, int direction, char nameFirst)
{
    return (spriteType == kTypeShadow || spriteType == kTypeBuilding) && (direction == 1 || direction == 3) &&
            (nameFirst == 's' || nameFirst == 'l');
}

bool DrawOffsetX(int spriteType, int direction, const AnimMetrics& current, const AnimMetrics* first, int* offset)
{
    if (spriteType != kTypeShadow && spriteType != kTypeBuilding) {
        *offset = current.hotspotX;
        return true;
    }
    switch (direction) {
    case 0:
    case 2: *offset = current.hotspotX; return true;
    case 1:
    case 3:
        if (!UsesFirstAnim(spriteType, direction, current.nameFirst)) {
            *offset = -(current.hotspotX + current.width);
        } else {
            *offset = current.hotspotX - first->hotspotX * 2 - first->width;
        }
        return true;
    default: return false;
    }
}

Rect AnchorRect(ScreenPoint world, int cameraX, int cameraY, int offsetX, const AnimMetrics& current)
{
    const int left = world.x + cameraX + offsetX;
    const int top = world.y + cameraY + current.hotspotY;
    return {left, top, left + current.width, top + current.height};
}

uint16_t DrawDepth(uint32_t mapViewDepth, uint16_t tileDepthOffset)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(mapViewDepth + kDrawDepthBias) + tileDepthOffset);
}

uint32_t InWorldDepth(uint32_t mapViewDepth, uint16_t tileDepthOffset)
{
    return (mapViewDepth & 0xffffu) + kDrawDepthBias + tileDepthOffset;
}

bool NeedsTonedPalette(float scale, float blend)
{
    return scale < 1.0f || scale > 1.0f || blend > 0.0f;
}

} // namespace obl::render_geometry
