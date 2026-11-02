#pragma once

#include "game/ui/sprite.h"

#include <cstdint>

namespace obl::ui {
struct FootprintDepth {
    uint32_t far, origin;
};
FootprintDepth MeasureBuildingFootprint(const float* world, int tilesDir6, int tilesDir0);
} // namespace obl::ui
