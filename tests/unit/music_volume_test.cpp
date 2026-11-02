#include "unit_test.h"
#include "game/ui/music_volume.h"

using namespace obl::audio;

OBL_TEST_CASE("music.volume_clamp_and_silent_threshold")
{
    OBL_CHECK_EQ(ClampVolume(-1), 0);
    OBL_CHECK_EQ(ClampVolume(2), 1);
    OBL_CHECK_EQ(ClampVolume(0.5f), 0.5f);
    OBL_CHECK_EQ(MilesVolume(-1), 0);
    OBL_CHECK_EQ(MilesVolume(0.02f), 0);
    OBL_CHECK_EQ(MilesVolume(0.024f), 3);
    OBL_CHECK_EQ(MilesVolume(0.5f), 63);
    OBL_CHECK_EQ(MilesVolume(1), 127);
    OBL_CHECK_EQ(MilesVolume(2), 127);
}
