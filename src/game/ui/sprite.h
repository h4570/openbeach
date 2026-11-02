#pragma once

#include <cstdint>
#include "core/rect.h"

namespace obl::ui {

struct SpriteAnim {
    void Construct();
    void Destruct();
    void Unload();
    void LoadAll();
    void LoadDirection(int direction);
    uint8_t* GetFrame(int frame, int direction);

    char* m_name;
    float m_lastUsedTime;
    uint8_t m_loaded;
    uint16_t m_frames;
    uint8_t m_directions;
    uint16_t m_width, m_height;
    int16_t m_hotspotX, m_hotspotY;
    uint8_t m_directionIndexLoaded;
    uint32_t m_dataSize, m_dataOffset;
    uint32_t m_directionOffsets[8];
    uint16_t m_uniqueFrames;
    uint16_t* m_frameIndices;
    uint8_t m_allLoaded;
    uint8_t* m_allData;
    uint8_t* m_directionData[8];
    uint32_t* m_frameOffsets;
};

struct Sprite {
    void Construct();
    void Destruct();

    char* m_name;
    int32_t m_refCount;
    int32_t m_animCount;
    SpriteAnim** m_anims;
    int32_t m_paletteCount;
    uint16_t** m_palettes;
    int32_t m_type;
    int32_t m_paletteModifierId;
    uint8_t m_buildingExtra[4];
};

struct SpriteInstance;

struct SpriteBank {
    int Load();
    SpriteInstance* CreateInstance(const char* name);
    SpriteInstance* CreateObservedInstance(const char* name);
    void ReleaseInstance(void* instance);
    void UnloadStale(float now);

    void* m_headerFile;
    int32_t m_datFile;
    void* m_paletteModifiers;
    int32_t m_spriteCount;
    Sprite** m_sprites;
    char m_names[5][0x50];
};

struct SpriteInstance {
    void BakeBuilding(screen::Rect clip, const float world[3], int tilesDir0, int tilesDir6);
    void BakeBuildingRect(screen::Rect clip, const float world[3], int maskX, int maskY);
    void BakeMasked(const float world[3]);
    void BakeGhost(const float world[3], int tilesDir0, int tilesDir6);
    void DrawGroundTile(const float world[3], int tilesDir0, int tilesDir6);
    float SetAnimByName(const char* name, float startTime, const float* world);
    void UpdateAnimNoSound(float time);
    void UpdateAnim(float time, const float* world);
    void SetDirectionFromVector(float x, float y);
    void FaceTowards(float targetX, float targetY, float targetZ, float fromX, float fromY, float fromZ);
    void DrawClipped(int x, int y, uint32_t depth, float time);
    void DrawClippedToned(int x, int y, uint32_t depth, float time, float scale, float blend);
    void DrawInWorld(const float* world, float time);
    void DrawInWorldToned(const float* world, float time, float scale, float blend);
    void DrawInWorldTonedWithExtra(const float* world, float time, float scale, float blend, uint32_t extra);
    int GetDrawOffsetX() const;
    screen::Rect GetScreenAnchorRect(const float* world) const;
    void GetWorldDrawPos(const float* world, float time, int* x, int* y, int16_t* depth);
    void Draw(int x, int y, int left, int top, int right, int bottom, uint32_t depth);
    void DrawBuildingPreview(const float* world, int tilesDir6, int tilesDir0, bool ghost);
    void DrawBuildingPreviewAlt(const float* world, int tilesDir6, int tilesDir0, float progress, bool lowerPart);
    void FindFirstOpaque(int* x, int* y) const;
    bool HitTest(int x, int y);

    char* m_name;
    float m_startTime;
    int32_t m_animType;
    int32_t m_fps;
    int32_t m_animIndex, m_frameIndex, m_direction;
    int32_t m_paletteIndex;
    uint8_t m_lit, m_keyline, m_triggeredSound;
    const uint16_t* m_overridePalette;
    Sprite* m_sprite;
    uint8_t m_actorRenderOption;
};

} // namespace obl::ui
