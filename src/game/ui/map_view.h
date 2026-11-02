#pragma once

#include "game/map/grid_math.h"
#include <bit>
#include <cstdint>
#include <deque>
#include <span>

namespace obl::object {
struct Base;
}
namespace obl::map_restore {
struct PixelRect;
}
namespace obl::ui {
struct Renderer;

struct MapBlock {
    uint8_t m_loaded;
    uint8_t m_usesWaterMask;
    uint32_t* m_pixels;
};

struct MapView {
    bool BuildTempMap(int blocksPerRow, int blocksPerColumn, int blockSize, float fade);
    void LoadBlock(int x, int y);
    void LoadBlockPair(uint32_t x, uint32_t y);
    void UnmapBlocks();
    bool UnmapAllBlocks();
    void UnloadFarBlocks();
    void ClampCamera();
    void AddCameraVelocity(float x, float y);
    void SetCameraPos(int x, int y);
    void ScrollToWorldPoint(float x, float y, float unused, float seconds);
    bool UpdateCamera();
    void LockCamera();
    void UnlockCamera();
    void StopCamera();
    void SetViewportSize(int width, int height);
    void ResetViewport();

    void SetDepthPlaneOffset(int value);
    void SetDepthSlantOffset(int value);
    uint32_t LoadWaterAnim(const char* path);
    bool ClipObjectRect(const object::Base* object);
    void DrawObjects(object::Base** list, int count, int unused);
    void RenderVisibleObjects(object::Base** list, int count);
    double GetVisibleTileRange();
    void DarkenByDepth();
    bool DrawSea();
    bool DrawTileHighlight(uint32_t tileX, uint32_t tileY, uint32_t color);
    MapView* Construct(void* owner, Renderer* renderer, const char* path);
    void Destruct();
    void RestoreAfterLoad(std::span<const int32_t> burnQueue);
    double GetTerrainHeight(int x, int y) const;
    double GetTerrainHeightF(int worldX, int worldY) const;
    uint32_t GetDepth(const float* world) const;
    int* TileToScreen(int* out, const uint32_t* tile) const;
    float* ScreenToWorldPoint(float* out, const int* screen) const;
    void QueueBlockRebuild(int blockX, int blockY, int width, int height);
    int GetRebuildQueueSize();
    void ProcessRebuildQueue(float time);
    void RestoreBackgroundRect(const map_restore::PixelRect* rect);
    void RestoreTileBackground(int worldX, int worldY, const int8_t* mask);
    void RestoreTileBackgroundRect(uint32_t blockX, uint32_t blockY, int offsetX, int offsetY, const int8_t* mask);
    void WorldToScreen(const float* world, int out[2]) const;
    int* ProjectWorldPoint(int* out, const float* world) const
    {
        WorldToScreen(world, out);
        return out;
    }
    static int WorldToInt(const float* world, int* outX, int* outY);
    static grid_math::TileXY WorldToTile(const float* world);
    static uint32_t* WorldToTile(uint32_t* out, const float* world)
    {
        const auto tile = WorldToTile(world);
        out[0] = tile.x;
        out[1] = tile.y;
        return out;
    }
    static int* WorldToTile(int* out, const float* world)
    {
        const auto tile = WorldToTile(world);
        out[0] = std::bit_cast<int32_t>(tile.x);
        out[1] = std::bit_cast<int32_t>(tile.y);
        return out;
    }

    void* m_tempFile;
    void* m_owner;
    int32_t m_viewLeft, m_viewTop;
    int32_t m_viewWidth, m_viewHeight;
    uint8_t m_blocksMapped;
    int32_t m_cameraX, m_cameraY;
    int32_t m_mapWidth, m_mapHeight;
    int32_t m_blocksPerRow;
    int32_t m_blocksPerColumn;
    int32_t m_blockSize;
    MapBlock* m_blocks;
    float m_cameraPositionX, m_cameraPositionY;
    float m_velocityX, m_velocityY;
    void* m_mappedFile;
    Renderer* m_renderer;
    uint8_t m_reuseTempMap;
    std::deque<int32_t> m_burnQueue;
    float m_lastUnBurnTime;
    uint8_t m_mapLocked;
    uint8_t m_movingMap;
    float m_moveMapDuration;
    float m_moveMapStartTime;
    int32_t m_moveMapStartX;
    int32_t m_moveMapStartY;
    int32_t m_moveMapDestX;
    int32_t m_moveMapDestY;
    float m_lastInputX, m_lastInputY;
    void* m_mapCache;
    int32_t m_guestObjectsDrawn;
    int32_t m_depthPlaneOffset;
    int32_t m_depthSlantOffset;
};

} // namespace obl::ui
