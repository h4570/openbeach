#include "line_draw.h"

namespace obl::line_draw {
namespace {

template<class Plot> void Shallow(const Line& line, uint32_t firstW, uint32_t firstH, uint32_t walkW, uint32_t walkH, int pitch, Plot plot)
{
    uint32_t x = static_cast<uint32_t>(line.x), y = static_cast<uint32_t>(line.y);
    const int inc = line.minor * 2;
    const int dec = inc - line.major * 2;
    int error = inc - line.major;
    const uint32_t upitch = static_cast<uint32_t>(pitch);
    if (x < firstW && y < firstH) plot(y * upitch + x);
    if (line.major == 0) return;
    uint32_t row = y * upitch;
    for (int count = line.major; count != 0; --count) {
        if (error >= 0) {
            ++y;
            row += upitch;
            error += dec;
        } else {
            error += inc;
        }
        x += static_cast<uint32_t>(line.step);
        if (x < walkW && y < walkH) plot(row + x);
    }
}

template<class Plot> void Steep(const Line& line, uint32_t width, uint32_t height, int pitch, Plot plot)
{
    uint32_t x = static_cast<uint32_t>(line.x), y = static_cast<uint32_t>(line.y);
    const int inc = line.major * 2;
    const int dec = inc - line.minor * 2;
    int error = inc - line.minor;
    const uint32_t upitch = static_cast<uint32_t>(pitch);
    if (x < width && y < height) plot(y * upitch + x);
    if (line.minor == 0) return;
    uint32_t row = y * upitch;
    for (int count = line.minor; count != 0; --count) {
        if (error >= 0) {
            x += static_cast<uint32_t>(line.step);
            error += dec;
        } else {
            error += inc;
        }
        row += upitch;
        ++y;
        if (x < width && y < height) plot(row + x);
    }
}

} // namespace

bool Prepare(int viewWidth, int viewHeight, int x0, int y0, int x1, int y1, Line* line)
{
    if (x0 >= viewWidth || x1 < 0 || y0 >= viewHeight || y1 <= 0) return false;
    if (y0 > y1) {
        const int tx = x0, ty = y0;
        x0 = x1;
        y0 = y1;
        x1 = tx;
        y1 = ty;
    }
    const int dy = y1 - y0;
    const int dx = x1 - x0;
    line->x = x0;
    line->y = y0;
    line->minor = dy;
    line->step = dx > 0 ? 1 : -1;
    line->major = dx > 0 ? dx : -dx;
    line->shallow = line->major > dy;
    return true;
}

void Draw32(const Line& line, uint32_t color, uint32_t* bits, int pitch, uint32_t width, uint32_t height)
{
    auto plot = [&](uint32_t index) { bits[index] = (bits[index] & 0xffff0000u) + (color & 0xffffu); };
    if (line.shallow)
        Shallow(line, width, height, width, height, pitch, plot);
    else
        Steep(line, width, height, pitch, plot);
}

void Draw16(const Line& line, uint32_t color, uint16_t* bits, int pitch, uint32_t width, uint32_t height)
{
    auto plot = [&](uint32_t index) { bits[index] = static_cast<uint16_t>(color); };
    if (line.shallow)
        Shallow(line, width, height, width - 1u, height - 1u, pitch, plot);
    else
        Steep(line, width, height, pitch, plot);
}

} // namespace obl::line_draw
