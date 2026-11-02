#include "unit_test.h"
#include "game/ui/spatial_audio.h"

OBL_TEST_CASE("sound.spatial_distance_and_listener")
{
    using namespace obl::audio;
    auto mix = SpatialGains({}, {}, 250, 750);
    OBL_CHECK_EQ(mix.left, 1);
    OBL_CHECK_EQ(mix.right, 1);
    mix = SpatialGains({0, 250, 0}, {}, 250, 750);
    OBL_CHECK_EQ(mix.left, 1);
    mix = SpatialGains({0, 0, 500}, {}, 250, 750);
    OBL_CHECK_EQ(mix.left, 0.5f);
    OBL_CHECK_EQ(mix.right, 0.5f);
    mix = SpatialGains({0, 0, 750}, {}, 250, 750);
    OBL_CHECK_EQ(mix.left, 0);
    mix = SpatialGains({100, 0, 0}, {}, 250, 750);
    OBL_CHECK_EQ(mix.left, 0);
    OBL_CHECK_EQ(mix.right, 1);
    mix = SpatialGains({100, 0, 0}, {200, 0, 0}, 250, 750);
    OBL_CHECK_EQ(mix.left, 1);
    OBL_CHECK_EQ(mix.right, 0);
    mix = SpatialGains({0, 1, 0}, {}, 0, 0);
    OBL_CHECK_EQ(mix.left, 0);
    mix = SpatialGains({0, 251, 0}, {}, 250, 100);
    OBL_CHECK_EQ(mix.left, 0);
}
