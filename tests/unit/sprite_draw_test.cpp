
#include "unit_test.h"

#include "game/ui/sprite_draw.h"

using namespace obl::sprite_draw;

namespace {

Blitter Pick(int type, int zoom, bool direct = false, int animIndex = 0, const char* name = "idle")
{
    return Choose({type, animIndex, name, direct, zoom});
}

std::ostream& operator<<(std::ostream& out, Blitter b)
{
    return out << static_cast<int>(b);
}

} // namespace

OBL_TEST_CASE("sprite_draw.simple_types_ignore_zoom")
{
    for (int zoom = 0; zoom <= 3; ++zoom) {
        OBL_CHECK_EQ(Pick(1, zoom), Blitter::Ui32);
        OBL_CHECK_EQ(Pick(2, zoom), Blitter::Type2);
        OBL_CHECK_EQ(Pick(3, zoom), Blitter::Actor);
        OBL_CHECK_EQ(Pick(4, zoom), Blitter::Shadow);
        OBL_CHECK_EQ(Pick(6, zoom), Blitter::Translucent);
    }
}

OBL_TEST_CASE("sprite_draw.direct_mode_only_redirects_ui")
{
    OBL_CHECK_EQ(Pick(1, 1, true), Blitter::Ui16Direct);
    for (int type : {2, 3, 4, 6, 0xc}) {
        OBL_CHECK_EQ(Pick(type, 1, true), Pick(type, 1, false));
    }
    // A building shadow in direct mode still goes to the scene buffer.
    OBL_CHECK_EQ(Pick(5, 1, true, 2, "shade"), Blitter::Shadow);
}

OBL_TEST_CASE("sprite_draw.type12_zoom")
{
    OBL_CHECK_EQ(Pick(0xc, 0), Blitter::Type12);
    OBL_CHECK_EQ(Pick(0xc, 1), Blitter::Type12);
    OBL_CHECK_EQ(Pick(0xc, 2), Blitter::Type12ZoomOut);
    OBL_CHECK_EQ(Pick(0xc, 3), Blitter::Type12);
    OBL_CHECK_EQ(kType12DepthBias, 0x102u);
}

OBL_TEST_CASE("sprite_draw.buildings_first_two_anims_and_lightmap_are_opaque")
{
    for (int zoom = 0; zoom <= 3; ++zoom) {
        OBL_CHECK_EQ(Pick(5, zoom, false, 0, "shade"), Blitter::Ui32);
        OBL_CHECK_EQ(Pick(5, zoom, false, 1, "shade"), Blitter::Ui32);
        OBL_CHECK_EQ(Pick(5, zoom, false, 2, "lightmap"), Blitter::Ui32);
        OBL_CHECK_EQ(Pick(5, zoom, false, 7, "LIGHTMAP"), Blitter::Ui32);
        OBL_CHECK_EQ(Pick(5, zoom, false, 2, "LightMap"), Blitter::Ui32);
    }
    // The name is not even read for the first two animations.
    OBL_CHECK_EQ(Choose({5, 1, nullptr, false, 1}), Blitter::Ui32);
}

OBL_TEST_CASE("sprite_draw.buildings_other_anims_are_shadows_by_zoom")
{
    OBL_CHECK_EQ(Pick(5, 0, false, 2, "shade"), Blitter::Shadow);
    OBL_CHECK_EQ(Pick(5, 1, false, 3, "shade"), Blitter::Shadow);
    OBL_CHECK_EQ(Pick(5, 2, false, 2, "shade"), Blitter::ShadowZoomOut);
    OBL_CHECK_EQ(Pick(5, 3, false, 2, "shade"), Blitter::None);
    OBL_CHECK_EQ(Pick(5, -1, false, 2, "shade"), Blitter::None);
    // Near misses of the name are shadows, not lightmaps.
    OBL_CHECK_EQ(Pick(5, 1, false, 2, "lightmap2"), Blitter::Shadow);
    OBL_CHECK_EQ(Pick(5, 1, false, 2, "lightma"), Blitter::Shadow);
    OBL_CHECK_EQ(Pick(5, 1, false, 2, ""), Blitter::Shadow);
}

OBL_TEST_CASE("sprite_draw.unrecognized_types_draw_nothing")
{
    for (int type : {-1, 0, 7, 8, 9, 10, 11, 13, 14, 100}) {
        for (int zoom = 0; zoom <= 2; ++zoom) OBL_CHECK_EQ(Pick(type, zoom), Blitter::None);
    }
}

OBL_TEST_CASE("sprite_draw.lightmap_name_match")
{
    OBL_CHECK(IsLightmapName("lightmap"));
    OBL_CHECK(IsLightmapName("LIGHTMAP"));
    OBL_CHECK(IsLightmapName("LiGhTmAp"));
    OBL_CHECK(!IsLightmapName(""));
    OBL_CHECK(!IsLightmapName("lightmap "));
    OBL_CHECK(!IsLightmapName("light"));
    OBL_CHECK(!IsLightmapName("lightmaps"));
    OBL_CHECK(!IsLightmapName(nullptr));
}

OBL_TEST_CASE("sprite_draw.destination_halving")
{
    OBL_CHECK(HalvesDestination(Blitter::ShadowZoomOut, 2));
    OBL_CHECK(HalvesDestination(Blitter::Type12ZoomOut, 2));
    OBL_CHECK(HalvesDestination(Blitter::Actor, 2));
    OBL_CHECK(!HalvesDestination(Blitter::Actor, 1));
    OBL_CHECK(!HalvesDestination(Blitter::Actor, 0));
    OBL_CHECK(!HalvesDestination(Blitter::Shadow, 2));
    OBL_CHECK(!HalvesDestination(Blitter::Type12, 2));
    OBL_CHECK(!HalvesDestination(Blitter::Ui32, 2));
    OBL_CHECK(!HalvesDestination(Blitter::Translucent, 2));
}
