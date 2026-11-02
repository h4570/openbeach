#pragma once

#include <cstdint>

namespace obl::line_draw {

// A line after the dispatcher's normalisation: the start is the upper end (y0 <= y1). Both loops take (|dx|, dy, step); the shallow one
// walks |dx| columns, the steep one dy rows.
struct Line {
    int x = 0, y = 0;
    int major = 0; // |dx|
    int minor = 0; // dy (>= 0)
    int step = 1; // +1, or -1 when dx <= 0 (a vertical line steps left, it never moves)
    bool shallow = false; // |dx| > dy: one pixel per column, y advances now and then
};

// The dispatcher's early outs and normalisation. False when the line is rejected (start at or right of `viewWidth` / below `viewHeight`,
// end left of 0 or at/above the first row; the test uses the endpoints as given, before the swap that puts the upper end first).
bool Prepare(int viewWidth, int viewHeight, int x0, int y0, int x1, int y1, Line* line);

// Scene buffer (32-bit words: low 16 bits colour, high 16 bits depth): writes the low 16 bits only. `pitch` in words; `width`/`height` are
// the renderer's scene size; every plot is clipped with an unsigned x < width and y < height test (negative coordinates fall out).
void Draw32(const Line& line, uint32_t color, uint32_t* bits, int pitch, uint32_t width, uint32_t height);

void Draw16(const Line& line, uint32_t color, uint16_t* bits, int pitch, uint32_t width, uint32_t height);

} // namespace obl::line_draw

namespace obl::ui::pixel_primitives {
// Uses the renderer's current buffer, clip and direct-surface origin.
void DrawLine(void* renderer, int x0, int y0, int x1, int y1, uint32_t color);
} // namespace obl::ui::pixel_primitives
