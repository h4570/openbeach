#pragma once

#include <cstdint>

namespace obl::sprite_draw {

enum class Blitter : uint8_t {
    None, // nothing is drawn (unknown sprite type, unsupported zoom, no back buffer)
    Ui32,
    Ui16Direct,
    Type2,
    Actor,
    Shadow,
    ShadowZoomOut,
    Translucent,
    Type12,
    Type12ZoomOut,
};

// Sprite kinds with a drawing path; unsupported kinds draw nothing.
inline constexpr int kTypeUi = 1, kTypeType2 = 2, kTypeActor = 3, kTypeShadow = 4, kTypeBuilding = 5, kTypeTranslucent = 6,
                     kTypeType12 = 0xc;
// Zoom levels: 0 zoomed in, 1 normal, 2 zoomed out.
inline constexpr int kZoomOut = 2;
// Added to the caller's depth for type 0xc sprites.
inline constexpr uint32_t kType12DepthBias = 0x102;

struct Inputs {
    int spriteType; // Sprite::m_type
    int animIndex; // SpriteInstance::m_animIndex
    const char* animName; // SpriteAnim::m_name; read only for type 5 with animIndex >= 2
    bool directMode; // draw into the back buffer instead of the scene buffer
    int zoomLevel; // zoom level of the view
};

bool IsLightmapName(const char* name);

// Direct mode only changes type 1. Type 5 buildings: the first two animations and the "lightmap" animation are
// drawn opaque, every other one is the building's shadow (cast on the ground), whose blitter depends on zoom.
Blitter Choose(const Inputs& in);

// True when the chosen blitter works at half resolution (destination coordinates are halved) for `zoomLevel`:
// ShadowZoomOut/Type12ZoomOut, and Actor sprites, whose ActorSelect picks a ZoomOut variant at zoom level 2.
bool HalvesDestination(Blitter blitter, int zoomLevel);

} // namespace obl::sprite_draw
