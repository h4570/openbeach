#pragma once

#include "core/rect.h"

#include <cstdint>
#include <string>

namespace obl::path_tiles {

inline constexpr int32_t kFullTileFrame = 0x43; // the full-tile frame when the tileset metadata names none
inline constexpr int32_t kNoGrassSurface = 999999;
inline constexpr int32_t kMaxSurfaces = 9; // the surface variants the constructor adds (path_tiles::kSurfaceNames)

} // namespace obl::path_tiles

// The ground appearance of the paths (docs/architecture/map.md). The world owns one (made by
// path::CreatePathTiles and kept in game::Context).
namespace obl::path {

// A ground sprite the path tiles hold. Opaque here: the sprite bank, its frames and drawing belong to the presentation,
// reached through the Env of path_tiles_lifecycle.h / path_tiles_rebuild.h; the game Env stores its SpriteInstance* in it.
enum class SpriteHandle : uintptr_t { None = 0 };

struct PathTiles {
    SpriteHandle m_pavement = SpriteHandle::None; // WORLD_PAVEMENT_TILESET, default PathTile_10
    SpriteHandle m_grass = SpriteHandle::None; // Grass08
    SpriteHandle m_surfaces[path_tiles::kMaxSurfaces] = {}; // in the order of path_tiles::kSurfaceNames
    SpriteHandle m_beachEdge = SpriteHandle::None; // WORLD_BEACH_EDGE_TILESET, default PathTile_10b
    uint8_t m_lookup[256] = {}; // neighbour mask -> frame, filled from the pavement sprite autotile metadata
    int32_t m_lastSurface = 0; // SetSurface and RebuildTile store the last chosen surface here, 0-based
    int32_t m_surfaceCount = 0; // filled entries of m_surfaces
    int32_t m_grassSurfaceNumber = path_tiles::kNoGrassSurface; // until Grass08 is added, then its 1-based surface number (7)
    int32_t m_fullTileFrame = path_tiles::kFullTileFrame; // from the pavement autotile metadata
    // The sprite names the queries answer; they live as long as the tiles.
    std::string m_pavementName;
    std::string m_grassName;

    // ---- the tile getters of the placement preview and of the image list (path_tiles_lifecycle.cpp) ----
    int GetTilesetNumber() const;
    const char* GetGrassSpriteName() const;
    const char* GetPavementSpriteName() const;
    int GetFullTileFrame() const;

    // ---- redraws and flag changes (path_tiles_rebuild.cpp) ----
    // The rebuilds redraw the tiles of the rectangle or of the map and change no grid flags; SetSurface and the neighbour
    // walks change them.
    void RebuildTile(uint32_t x, uint32_t y, int32_t removeNeighbours);
    void RebuildAll();
    void RebuildRect(const screen::Rect& rect);
    void UpdateBuildingOverlap(uint32_t x, uint32_t y);
    void SetSurface(uint32_t x, uint32_t y, int32_t surface);
    void AddNeighbourBits(uint32_t x, uint32_t y);
    void RemoveNeighbourBits(uint32_t x, uint32_t y);
};

} // namespace obl::path
