#pragma once

#include <cstdint>

namespace obl::sprite_scan {

// Name of the building animation that holds the cursor-hit mask (a run-only copy of the silhouette).
inline constexpr const char kPickAnimName[] = "pick";

struct Opaque {
    int x, y;
};

// Input frames contain complete rows of signed run lengths and their palette bytes.
// Scanning also reads the byte after the last literal run it passes.
bool FindFirstOpaque(const uint8_t* frame, int width, int height, Opaque* result);

// Is the pixel at (x, y) opaque? `maskOnly` frames (the `pick` animation of buildings) hold runs without palette
// bytes. The column test is 1-based: it answers for the pixel *before* column x, so x <= 0 is always false and a
// run ending exactly at x decides, which makes the hit one pixel to the left of what the coordinates suggest.
// A negative `y` skips no rows. The scan is unbounded: x at or beyond the width spills into the following rows.
bool HitTest(const uint8_t* frame, int width, int x, int y, bool maskOnly);

} // namespace obl::sprite_scan
