#include "core/ascii_case.h"
#include "game/ui/sprite_draw.h"

namespace obl::sprite_draw {

bool IsLightmapName(const char* name)
{
    return name && ascii::EqualsNoCase(name, "lightmap");
}

Blitter Choose(const Inputs& in)
{
    switch (in.spriteType) {
    case kTypeUi: return in.directMode ? Blitter::Ui16Direct : Blitter::Ui32;
    case kTypeType2: return Blitter::Type2;
    case kTypeActor: return Blitter::Actor;
    case kTypeShadow: return Blitter::Shadow;
    case kTypeBuilding:
        if (in.animIndex < 2 || IsLightmapName(in.animName)) return Blitter::Ui32;
        if (in.zoomLevel == 0 || in.zoomLevel == 1) return Blitter::Shadow;
        return in.zoomLevel == kZoomOut ? Blitter::ShadowZoomOut : Blitter::None;
    case kTypeTranslucent: return Blitter::Translucent;
    case kTypeType12: return in.zoomLevel == kZoomOut ? Blitter::Type12ZoomOut : Blitter::Type12;
    default: return Blitter::None;
    }
}

bool HalvesDestination(Blitter blitter, int zoomLevel)
{
    switch (blitter) {
    case Blitter::ShadowZoomOut:
    case Blitter::Type12ZoomOut: return true;
    case Blitter::Actor: return zoomLevel == kZoomOut;
    default: return false;
    }
}

} // namespace obl::sprite_draw
