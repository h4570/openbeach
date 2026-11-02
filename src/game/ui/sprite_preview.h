#pragma once

#include <cstdint>

namespace obl::sprite_preview {

float DepthPerPixel(int depthDifference, uint8_t extent);

// The part of the frame the Alt variant draws. `dstY` follows `top` (the blitters take the already clipped
// destination origin).
struct Window {
    int dstY, top, bottom;
};

// Alt, building, mode 0 (draws the upper part): bottom = min(bottom, trunc((1.0 - progress) * height)).
void ClipToUpperPart(Window* window, float progress, int height);
// Alt, building, mode != 0 (draws the lower part): newTop = height - trunc(height * progress); when that is below
// the current top the destination moves down by the same amount.
void ClipToLowerPart(Window* window, float progress, int height);
// Alt, other sprite types: bottom = min(bottom, trunc((progress - 1.0f) * height)); note the sign, which differs
// from the building case (it is `progress - 1`, so for progress < 1 nothing is left to draw).
void ClipFlat(Window* window, float progress, int height);

} // namespace obl::sprite_preview
