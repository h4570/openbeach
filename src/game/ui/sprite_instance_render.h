#pragma once

#include "game/ui/renderer_types.h"
#include "game/ui/sprite.h"

#include <cstddef>
#include <cstdint>

namespace obl::ui {
// The span of work-buffer words (colour + depth) one frame draw may touch, for pixel checks: from the destination
// origin through the last row plus a guard pixel; `half` for the ZoomOut blitters, which halve the destination.
struct Region {
    size_t first = 0, count = 0;
};
Region TouchedRegion(const ui::Renderer& r, int x, int y, int width, int height, bool half);

} // namespace obl::ui
