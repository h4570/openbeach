#include "game/map/path_tiles_rebuild.h"

#include "game/game/game_accessors.h"
#include "game/map/grid_state.h"
#include "game/map/path_queries.h"
#include "game/map/path_tiles_state.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/gui_tooltip.h"
#include "game/ui/map_view.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_restore.h"
#include "game/ui/renderer_ops.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_anim.h"
#include "game/ui/sprite_instance_bake.h"
#include "game/ui/sprite_instance_geometry.h"

#include <cstdint>

namespace obl::path {
namespace {

using screen::Rect;
using ui::SpriteInstance;

map::Grid& WorldGrid()
{
    return *game::GetGrid();
}

// The game behind every call the rebuild logic makes; the sibling rebuilds go through the PathTiles members.
struct GameEnv {
    PathTiles& tiles;

    uint32_t& Cell(uint32_t x, uint32_t y) { return WorldGrid().Cell(x, y).m_flags; }
    void Neighbour(uint32_t x, uint32_t y, int direction, uint32_t out[2])
    {
        const grid_math::TileXY next = WorldGrid().GetNeighbour(x, y, direction);
        out[0] = next.x;
        out[1] = next.y;
    }
    void TileToWorld(uint32_t x, uint32_t y, float out[3])
    {
        const grid_math::WorldPoint point = WorldGrid().TileToWorldPoint(x, y, 1);
        out[0] = point.x;
        out[1] = point.y;
        out[2] = point.z;
    }
    void WorldToScreen(const float world[3], int out[2]) { ui::GetMapView()->ProjectWorldPoint(out, world); }
    int CameraX() { return ui::GetMapView()->m_cameraX; }
    int CameraY() { return ui::GetMapView()->m_cameraY; }
    int FullTileFrame() { return tiles.GetFullTileFrame(); }
    const int8_t* FullTileMask(SpriteHandle pavement)
    {
        return reinterpret_cast<const int8_t*>((SpriteOf(pavement)->m_sprite->m_anims[0])->GetFrame(FullTileFrame(), 0));
    }
    void RestoreTileBackground(int x, int y, const int8_t* mask) { ui::GetMapView()->RestoreTileBackground(x, y, mask); }
    void DrawGroundTile(SpriteHandle handle, int frame, const float world[3])
    {
        SpriteInstance* sprite = SpriteOf(handle);
        sprite->m_frameIndex = frame;
        (sprite)->DrawGroundTile(world, 1, 1);
    }
    void RebuildTile(uint32_t x, uint32_t y, int32_t removeNeighbours) { tiles.RebuildTile(x, y, removeNeighbours); }
    void AddNeighbourBits(uint32_t x, uint32_t y) { tiles.AddNeighbourBits(x, y); }
    void RemoveNeighbourBits(uint32_t x, uint32_t y) { tiles.RemoveNeighbourBits(x, y); }
    void UpdateBuildingOverlap(uint32_t x, uint32_t y) { tiles.UpdateBuildingOverlap(x, y); }
    int DrawOffsetX(SpriteHandle sprite) { return (SpriteOf(sprite))->GetDrawOffsetX(); }
    void AnimBox(SpriteHandle handle, int& hotspotY, int& width, int& height)
    {
        const SpriteInstance& sprite = *SpriteOf(handle);
        const ui::SpriteAnim& anim = *sprite.m_sprite->m_anims[sprite.m_animIndex];
        hotspotY = static_cast<int>(anim.m_hotspotY);
        width = static_cast<int>(anim.m_width);
        height = static_cast<int>(anim.m_height);
    }
    object::Base** Scenery(uint32_t& count)
    {
        m_scenery = object::object_manager::GetListByClass(game::GetObjectManager(), 1);
        count = static_cast<uint32_t>(m_scenery.size());
        return m_scenery.data();
    }
    bool Intersect(const Rect& a, const Rect& b, Rect& out) { return renderer_ops::IntersectRects(a, b, out); }
    void Bake(object::Scenery& scenery, const Rect& clip, const float world[3], int maskX, int maskY)
    {
        (static_cast<ui::SpriteInstance*>(scenery.m_sprite))->BakeBuildingRect(clip, world, maskX, maskY);
    }
    void ScreenRectToTileRect(const int rect[4], int& x, int& y, int& width, int& height)
    {
        WorldGrid().ScreenRectToTileRect(rect, &x, &y, &width, &height);
    }
    void SetLoadingProgress(float progress) { gui::LoadingBarSetProgress(progress); }

    object::object_manager::ObjectList m_scenery; // the last Scenery() result, read through the pointer it returned
};

} // namespace

void PathTiles::RebuildTile(uint32_t x, uint32_t y, int32_t removeNeighbours)
{
    GameEnv env{*this};
    path_tiles::RebuildTile(env, *this, x, y, removeNeighbours);
}

void PathTiles::RebuildAll()
{
    GameEnv env{*this};
    path_tiles::RebuildAll(env, *this);
}

// The rectangle is four int32 (x0, y0, x1, y1); the tile loop reads it as an int array.
void PathTiles::RebuildRect(const Rect& rect)
{
    GameEnv env{*this};
    path_tiles::RebuildRect(env, *this, reinterpret_cast<const int*>(&rect));
}

void PathTiles::UpdateBuildingOverlap(uint32_t x, uint32_t y)
{
    GameEnv env{*this};
    path_tiles::UpdateBuildingOverlap(env, *this, x, y);
}

void PathTiles::SetSurface(uint32_t x, uint32_t y, int32_t surface)
{
    GameEnv env{*this};
    path_tiles::SetSurface(env, *this, x, y, surface);
}

void PathTiles::AddNeighbourBits(uint32_t x, uint32_t y)
{
    GameEnv env{*this};
    path_tiles::AddNeighbourBits(env, *this, x, y);
}

void PathTiles::RemoveNeighbourBits(uint32_t x, uint32_t y)
{
    GameEnv env{*this};
    path_tiles::RemoveNeighbourBits(env, *this, x, y);
}

} // namespace obl::path
