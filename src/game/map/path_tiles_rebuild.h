#pragma once

#include "core/rect.h"
#include "game/objects/object_scenery.h"
#include "game/map/path_tiles_lifecycle.h"

#include <cstdint>
#include <cstring>

// The tile rebuilds of path::PathTiles: one tile, the whole map, a screen rectangle, the building overlap of a tile, and the
// surface and neighbour-bit changes. The grid flags they read and write are the k* constants below. No game memory here:
// path_tiles_rebuild.cpp supplies the Env, the unit tests a fake. Every call into the rest of the game is an Env call in a
// fixed order, so the recorded trace of a fake is the contract. The Env needs
//   uint32_t& Cell(uint32_t x, uint32_t y)                 the flags of a grid cell, wrapped modulo the grid size
//   void Neighbour(uint32_t x, uint32_t y, int direction, uint32_t out[2])
//                                                          Grid::GetNeighbour (out may be off the grid; the flags access wraps it)
//   void TileToWorld(uint32_t x, uint32_t y, float out[3]) Grid::TileToWorldPoint with anchor kind 1
//   void WorldToScreen(const float world[3], int out[2])   the map view's projection
//   int CameraX(), int CameraY()                           the map view's camera
//   int FullTileFrame()                                    full frame from the pavement tileset metadata
//   const int8_t* FullTileMask(SpriteHandle)               the full frame's mask (anim 0, direction 0)
//   void RestoreTileBackground(int x, int y, const int8_t* mask)   restores the map view background under the mask
//   void DrawGroundTile(SpriteHandle, int frame, const float world[3])   sets the sprite's frame (it stays set), then draws
//                                                          it as a ground tile with both tile counts 1
//   void RebuildTile(x, y, removeNeighbours), AddNeighbourBits(x, y), RemoveNeighbourBits(x, y), UpdateBuildingOverlap(x, y)
//                                                          the sibling rebuilds through the PathTiles members
//   int DrawOffsetX(SpriteHandle)                          the sprite's horizontal draw offset
//   void AnimBox(SpriteHandle, int& hotspotY, int& width, int& height)   the hotspot and size of the sprite's current animation
//   object::Base** Scenery(uint32_t& count)                the objects of class 1 (scenery)
//   bool Intersect(const Rect& a, const Rect& b, Rect& out) out is written only when they overlap
//   void Bake(Scenery&, const Rect& clip, const float world[3], int maskX, int maskY)   bakes the building rectangle
//   void ScreenRectToTileRect(const int rect[4], int& x, int& y, int& width, int& height)   Grid::ScreenRectToTileRect
//   void SetLoadingProgress(float)                         the loading bar
namespace obl::path_tiles {

// The grid flags the rebuilds use (a cell's m_flags).
inline constexpr uint32_t kFlagBeachEdge = 0x00000002; // the edge sprite replaces the pavement for the neighbour-lookup frame
inline constexpr uint32_t kFlagGrass = 0x00007000; // Grass08, frame 0
inline constexpr uint32_t kMaskNeighbours = 0x00ff0000; // the eight neighbour-path bits, the index of PathTiles::m_lookup after >> 16
inline constexpr uint32_t kFirstNeighbourBit = 0x00010000;
inline constexpr uint32_t kMaskSurface = 0x0f000000; // surface number, 1-based; 0 = none
inline constexpr uint32_t kFlagFullTile = 0x10000000; // a full path tile: frame 67 and the neighbours' bits
inline constexpr int kSurfaceShift = 24;
inline constexpr int kNeighbourCount = 8;
inline constexpr int kMaxTile = 0x7f; // tiles 1..127 in y and up to 127 in x are rebuilt; the map is 128 x 128
inline constexpr uint32_t kGridSize = 0x80;
// The progress `RebuildAll` reports every fourth column: x / 256 + 0.5, so the second half of the loading bar.
inline constexpr float kProgressStep = 0.00390625f;
inline constexpr float kProgressBase = 0.5f;

// The lookup byte is zero-extended and compared with 32-bit -1 (`no frame`), so the test never fires on a byte; it is kept
// to record that intent, and dropping it would not change a frame.
inline bool LookupAllowsRebuild(const path::PathTiles& tiles, uint32_t flags)
{
    const uint32_t lookup = tiles.m_lookup[(flags >> 16) & 0xff];
    return lookup != 0xffffffffu;
}

// The surface number goes into the four bits at 24 (a number above 15 spills into the full-tile bit; it is not masked) and
// the last-surface field follows it, 0-based.
template<class Env> void SetSurface(Env& env, path::PathTiles& tiles, uint32_t x, uint32_t y, int32_t surface)
{
    tiles.m_lastSurface = surface - 1;
    uint32_t& flags = env.Cell(x, y);
    flags = (flags & ~kMaskSurface) | (static_cast<uint32_t>(surface) << kSurfaceShift);
}

// The neighbour of the tile in direction (i + 4) % 8 gets the bit for direction i: the tile is that neighbour's neighbour in the opposite direction.
template<class Env> void AddNeighbourBits(Env& env, path::PathTiles& tiles, uint32_t x, uint32_t y)
{
    uint32_t bit = kFirstNeighbourBit;
    for (int i = 0; i < kNeighbourCount; ++i) {
        uint32_t neighbour[2];
        env.Neighbour(x, y, (i + 4) & 7, neighbour);
        uint32_t& cell = env.Cell(neighbour[0], neighbour[1]);
        cell |= bit;
        const uint32_t flags = cell; // read back after the OR
        if (!(flags & kFlagFullTile) && LookupAllowsRebuild(tiles, flags)) env.RebuildTile(neighbour[0], neighbour[1], 0);
        bit <<= 1;
    }
}

// The same walk clearing the bits. The decision to rebuild is taken on the flags as they were before the bit went away; the rebuild itself then sees them cleared.
template<class Env> void RemoveNeighbourBits(Env& env, path::PathTiles& tiles, uint32_t x, uint32_t y)
{
    uint32_t bit = kFirstNeighbourBit;
    for (int i = 0; i < kNeighbourCount; ++i) {
        uint32_t neighbour[2];
        env.Neighbour(x, y, (i + 4) & 7, neighbour);
        uint32_t& cell = env.Cell(neighbour[0], neighbour[1]);
        const uint32_t before = cell;
        cell = before & ~bit;
        if (!(before & kFlagFullTile) && LookupAllowsRebuild(tiles, before)) env.RebuildTile(neighbour[0], neighbour[1], 0);
        bit <<= 1;
    }
}

// The screen rectangle of the pavement frame at the tile (the sprite's draw offset and its animation's hotspot, width and
// height) is intersected with the screen rectangle of every scenery object (class 1) that is not in state 3, and the overlap, when not empty, is baked over
// again from the object's sprite so it stays in front of the ground that was just redrawn.
template<class Env> void UpdateBuildingOverlap(Env& env, path::PathTiles& tiles, uint32_t x, uint32_t y)
{
    float world[3];
    env.TileToWorld(x, y, world);
    int screen[2];
    env.WorldToScreen(world, screen);
    const int cameraX = env.CameraX();
    const int drawOffset = env.DrawOffsetX(tiles.m_pavement);
    int hotspotY = 0, width = 0, height = 0;
    env.AnimBox(tiles.m_pavement, hotspotY, width, height);
    screen::Rect rect;
    rect.left = drawOffset + cameraX + screen[0];
    rect.top = hotspotY + env.CameraY() + screen[1];
    rect.right = width + rect.left;
    rect.bottom = height + rect.top;

    uint32_t count = 0;
    object::Base** list = env.Scenery(count);
    for (uint32_t i = 0; i < count; ++i) {
        object::Scenery& scenery = *static_cast<object::Scenery*>(list[i]);
        if (scenery.m_state == 3) continue;
        screen::Rect overlap;
        if (!env.Intersect(rect, scenery.m_ScrXYRect, overlap) || overlap.top == overlap.bottom || overlap.left == overlap.right) continue;
        float position[3];
        std::memcpy(position, scenery.m_WorldXYZ, sizeof(position));
        env.Bake(scenery, overlap, position, overlap.left - rect.left, overlap.top - rect.top);
    }
}

// The tile is redrawn in the map blocks: the background under it is restored with the frame-67 mask, then grass, the chosen surface
// and either the full pavement tile (which also tells the neighbours) or the frame the neighbour bits select (the beach-edge sprite when the cell is on the
// beach). Tiles outside x <= 127 (negative x is not rejected: the test is signed and one-sided), 1 <= y <= 127, or whose screen position is off the left or
// top of the map, are left alone, and then the building overlap is not updated either. A surface number above the used
// slots is not drawn.
template<class Env> void RebuildTile(Env& env, path::PathTiles& tiles, uint32_t x, uint32_t y, int32_t removeNeighbours)
{
    const uint32_t flags = env.Cell(x, y);
    const int32_t tileX = static_cast<int32_t>(x);
    const int32_t tileY = static_cast<int32_t>(y);
    if (!(tileX <= kMaxTile && tileY >= 1 && tileY <= kMaxTile)) return;

    const int8_t* mask = env.FullTileMask(tiles.m_pavement);
    float world[3];
    env.TileToWorld(x, y, world);
    int screen[2];
    env.WorldToScreen(world, screen);
    const int pixelX = env.CameraX() - 0x1f + screen[0];
    const int pixelY = env.CameraY() - 0x20 + screen[1];
    if (pixelX < 0 || pixelY < 0) return;
    env.RestoreTileBackground(pixelX, pixelY, mask);

    if (flags & kFlagGrass) {
        env.TileToWorld(x, y, world);
        env.DrawGroundTile(tiles.m_grass, 0, world);
    }
    if (flags & kMaskSurface) {
        env.TileToWorld(x, y, world);
        const int32_t index = static_cast<int32_t>((flags >> kSurfaceShift) & 0xf) - 1;
        tiles.m_lastSurface = index;
        if (index < tiles.m_surfaceCount) env.DrawGroundTile(tiles.m_surfaces[index], 0, world);
    }
    if (flags & kFlagFullTile) {
        env.TileToWorld(x, y, world);
        env.DrawGroundTile(tiles.m_pavement, env.FullTileFrame(), world);
        env.AddNeighbourBits(x, y);
    } else if (flags & kMaskNeighbours) {
        const int32_t frame = tiles.m_lookup[(flags >> 16) & 0xff];
        const path::SpriteHandle sprite = (flags & kFlagBeachEdge) ? tiles.m_beachEdge : tiles.m_pavement;
        env.TileToWorld(x, y, world);
        env.DrawGroundTile(sprite, frame, world);
    }
    if (removeNeighbours) env.RemoveNeighbourBits(x, y);
    env.UpdateBuildingOverlap(x, y);
}

// Every tile of the map with any flag set, column by column; after each fourth column the loading bar moves.
template<class Env> void RebuildAll(Env& env, path::PathTiles&)
{
    for (uint32_t x = 1; x < kGridSize; ++x) {
        for (uint32_t y = 1; y < kGridSize; ++y)
            if (env.Cell(x, y) != 0) env.RebuildTile(x, y, 0);
        if ((x & 3) == 0) env.SetLoadingProgress(static_cast<float>(static_cast<int32_t>(x)) * kProgressStep + kProgressBase);
    }
}

// The tiles under a screen rectangle (a map block when the blocks are restored), row by row.
template<class Env> void RebuildRect(Env& env, path::PathTiles&, const int* rect)
{
    int x0, y0, width, height;
    env.ScreenRectToTileRect(rect, x0, y0, width, height);
    for (int y = y0; y < y0 + height; ++y)
        for (int x = x0; x < x0 + width; ++x) env.RebuildTile(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0);
}

} // namespace obl::path_tiles
