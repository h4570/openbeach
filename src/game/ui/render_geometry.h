#pragma once

#include <cstdint>

// Screen geometry of sprites in the world: the arithmetic of MapView::WorldToScreen and the SpriteInstance position
// readers (GetDrawOffsetX, GetScreenAnchorRect, GetWorldDrawPos). Pure functions over numbers the callers gather from
// the game objects.
namespace obl::render_geometry {

// The world Y axis is flattened by this factor before it becomes a map row (a float in the data section).
inline constexpr double kWorldYScale = 0.5;
// GetWorldDrawPos adds this to MapView::GetDepth.
inline constexpr int kDrawDepthBias = 8;
// Sprite type values the offset code distinguishes (Actor 3 also adds a tile depth offset in GetWorldDrawPos).
inline constexpr int kTypeActor = 3, kTypeShadow = 4, kTypeBuilding = 5;

// Float to int: truncation toward zero to 64 bits, low word returned: values beyond int wrap, NaN and values outside int64 give 0.
int FloatToIntWrap(double value);

// Map column / row of a world point (world X is a map column, world Y halved is a map row).
int WorldToMapX(float worldX);
int WorldToMapY(float worldY);

struct ScreenPoint {
    int x, y;
};
// `terrainHeight` is the truncated MapView::GetTerrainHeight at the map point; cameraX/cameraY are the map view camera.
ScreenPoint MapToScreen(int mapX, int mapY, int terrainHeight, int cameraX, int cameraY);

// The SpriteAnim fields the readers use (hotspot, size, first byte of the name).
struct AnimMetrics {
    int hotspotX, hotspotY;
    int width, height;
    char nameFirst;
};

// Types 4 (shadows) and 5 (buildings) mirror the sprite for directions 1 and 3; for animations whose name starts
// with 's' or 'l' the mirror pivots on the sprite's first animation instead of the current one.
bool UsesFirstAnim(int spriteType, int direction, char nameFirst);

// SpriteInstance::GetDrawOffsetX: horizontal offset of the drawn frame from the world point. Returns false for
// types 4/5 with a direction above 3 (also negative): there is no offset then and `*offset` is untouched. `first` is read only when UsesFirstAnim() holds.
bool DrawOffsetX(int spriteType, int direction, const AnimMetrics& current, const AnimMetrics* first, int* offset);

struct Rect {
    int left, top, right, bottom;
};
// SpriteInstance::GetScreenAnchorRect: the frame's rectangle in map pixels (camera added back).
// `world` is MapToScreen() of the world point.
Rect AnchorRect(ScreenPoint world, int cameraX, int cameraY, int offsetX, const AnimMetrics& current);

// SpriteInstance::GetWorldDrawPos: draw depth as the game stores it, a 16-bit word that wraps. `tileDepthOffset` is
// the Grid cell's depth offset for actors (type 3) and 0 for every other type.
uint16_t DrawDepth(uint32_t mapViewDepth, uint16_t tileDepthOffset);

// SpriteInstance::DrawInWorld: depth handed to SpriteInstance::Draw. Unlike GetWorldDrawPos it keeps 32 bits:
// the low 16 bits of MapView::GetDepth, plus the bias and the actor's tile depth offset.
uint32_t InWorldDepth(uint32_t mapViewDepth, uint16_t tileDepthOffset);

// SpriteInstance::DrawInWorldToned/TonedEx build a toned palette only when the tone differs from the neutral one: `scale` is
// not exactly 1.0 (a NaN counts as equal) or `blend` is above 0. Both values feed BuildTonedPalette (palette_modifiers.h).
bool NeedsTonedPalette(float scale, float blend);

} // namespace obl::render_geometry
