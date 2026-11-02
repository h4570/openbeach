#include "blit.h"

#include <algorithm>
#include <cstdlib>

namespace obl::blit {
namespace {

template<class Pixel> void DrawRle(Pixel* dst, const uint8_t* rle, const uint16_t* palette, RleRect r, bool translucent, Masks masks)
{
    // Ui32's left-clipped branch consumes the stream but never writes; Ui16 still draws.
    if constexpr (sizeof(Pixel) == 4) {
        if (!translucent && r.left > 0) return;
    }
    const bool full = r.left <= 0 && r.top <= 0 && r.width <= r.right && r.height <= r.bottom;
    const int firstRow = std::max(0, r.top);
    const int rows = full ? r.height : r.bottom - r.top;
    const int left = std::max(0, r.left);
    // Every branch that draws with a positive left bound ignores the right bound.
    const int right = full || left > 0 ? r.width : r.right;
    if (rows <= 0 || r.width <= 0) return;
    for (int row = 0; row < firstRow + rows; ++row) {
        int x = 0;
        while (x < r.width) {
            const int run = static_cast<int8_t>(*rle++);
            if (run <= 0) {
                x -= run;
                continue;
            }
            for (int i = 0; i < run; ++i, ++x) {
                const uint8_t index = *rle++;
                if (row < firstRow || x < left || x >= right) continue;
                Pixel& out = dst[(r.y + row - firstRow) * r.pitch + r.x + x - left];
                const uint16_t color = translucent ? Blend(index, static_cast<uint16_t>(out), palette, masks) : palette[index];
                if constexpr (sizeof(Pixel) == 4)
                    out = 0xffff0000u | color;
                else
                    out = color;
            }
        }
    }
}

} // namespace

uint16_t Blend(uint8_t index, uint16_t background, const uint16_t* palette, Masks masks)
{
    const uint16_t color = palette[index & 63];
    const auto half = [masks](uint16_t v) { return (v & masks.half) >> 1; };
    const auto quarter = [masks](uint16_t v) { return (v & masks.quarter) >> 2; };
    switch (index >> 6) {
    case 0: return color;
    case 1: return static_cast<uint16_t>(half(color) + quarter(color) + quarter(background));
    case 2: return static_cast<uint16_t>(half(color) + half(background));
    default: return static_cast<uint16_t>(half(background) + quarter(color) + quarter(background));
    }
}

void Ui(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect r)
{
    DrawRle(dst, rle, palette, r, false, {});
}

void Ui(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect r)
{
    DrawRle(dst, rle, palette, r, false, {});
}

void Translucent(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect r, Masks masks)
{
    DrawRle(dst, rle, palette, r, true, masks);
}

void Type2(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect r)
{
    if (!(r.left <= 0 && r.top <= 0 && r.width <= r.right && r.height <= r.bottom)) {
        Ui(dst, rle, palette, r);
        return;
    }
    for (int row = 0; row < r.height; ++row) {
        bool connected = false;
        int x = 0;
        while (x < r.width) {
            const int run = static_cast<int8_t>(*rle++);
            if (run < 0) {
                x -= run;
                connected = false;
                continue;
            }
            // Legal Type2 streams contain positive literals, never zero controls.
            for (int i = 0; i < run; ++i, ++x) {
                const uint16_t color = i == 0 && !connected ? static_cast<uint16_t>(r.width) : palette[*rle];
                ++rle;
                auto& out = dst[(r.y + row) * r.pitch + r.x + x];
                out = (out & 0xffff0000u) | color;
            }
            connected = true;
        }
    }
}

void RleToBytes(uint8_t* dst, const uint8_t* rle, RleRect r, bool zoom)
{
    const bool full = r.left <= 0 && r.top <= 0 && r.width <= r.right && r.height <= r.bottom;
    const int first = std::max(0, r.top), rows = full ? r.height : r.bottom - r.top;
    for (int row = 0; row < first + rows; ++row) {
        int x = 0, outX = 0, skip = std::max(0, r.left);
        const int local = row - first;
        const bool draw = row >= first && (!zoom || ((r.y + local) & 1) == 0);
        const int outY = zoom ? r.y / 2 + local / 2 : r.y + local;
        while (x < r.width) {
            const int run = static_cast<int8_t>(*rle++);
            const int length = std::abs(run);
            for (int i = 0; i < length; ++i, ++x) {
                const int value = run > 0 ? static_cast<int>(*rle++) - 24 : 0;
                if (row < first) continue;
                if (skip > 0) {
                    --skip;
                    continue;
                }
                const bool sample = draw && (!zoom || ((r.x + x) & 1) == 0);
                if (run < 0) {
                    // Left-clipped gaps ignore right; unzoomed right-only gaps
                    // advance the whole run when its start is before right.
                    if (sample && (zoom || r.left > 0 || full || x - i < r.right)) ++outX;
                } else if (full || x < r.right) {
                    if (sample) {
                        auto& out = dst[outY * r.pitch + (zoom ? r.x / 2 : r.x) + outX];
                        if (out < value) out = static_cast<uint8_t>(value);
                        ++outX;
                    }
                }
            }
        }
    }
}

void RleClear(uint8_t* dst, const uint8_t* runs, RleRect r)
{
    const bool full = r.left <= 0 && r.top <= 0 && r.width <= r.right && r.height <= r.bottom;
    const int first = std::max(0, r.top), rows = full ? r.height : r.bottom - r.top;
    for (int row = 0; row < first + rows; ++row) {
        int x = 0, outX = 0, skip = std::max(0, r.left);
        while (x < r.width) {
            const int run = static_cast<int8_t>(*runs++);
            for (int i = 0; i < std::abs(run); ++i, ++x) {
                if (row < first) continue;
                if (skip > 0) {
                    --skip;
                    continue;
                }
                if (run < 0) {
                    if (r.left > 0 || full || x - i < r.right) ++outX;
                } else if (full || x < r.right) {
                    dst[(r.y + row - first) * r.pitch + r.x + outX++] = 0;
                }
            }
        }
    }
}

void MaxBytes(uint8_t* dst, const uint8_t* src, RleRect r)
{
    for (int row = 0; row < r.bottom - r.top; ++row)
        for (int x = r.left; x < r.right; ++x) {
            auto& out = dst[(r.y + row) * r.pitch + r.x + x - r.left];
            out = std::max(out, src[(r.top + row) * r.width + x]);
        }
}

void Translucent(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect r, Masks masks)
{
    DrawRle(dst, rle, palette, r, true, masks);
}

void Depth(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect r, uint32_t depth, int slant, Masks masks, DepthKind kind,
        bool zoom, uint16_t keyline, uint8_t* light, int lightPitch)
{
    const bool full = r.left <= 0 && r.top <= 0 && r.width <= r.right && r.height <= r.bottom;
    const bool type12 = kind == DepthKind::Type12;
    const bool lit = kind == DepthKind::Lit;
    const bool actor = kind == DepthKind::Actor;
    const int firstRow = std::max(0, r.top);
    const int rows = full ? r.height : r.bottom - r.top;
    if (rows <= 0 || r.width <= 0) return;
    // Use unsigned arithmetic to reproduce the game's wrapping high-word depth accumulator.
    if (zoom && actor && (r.y & 1)) depth += 8;
    uint32_t z = ((depth & 0xffffu) - (type12 ? r.height * 16u : slant * 8u)) << 16;
    for (int row = 0; row < firstRow + rows; ++row, ++slant) {
        z += type12 || slant >= 0 ? 0x100000u : 0xfff80000u;
        const int localRow = row - firstRow;
        const bool drawRow = row >= firstRow && (!zoom || ((localRow & 1) == (full && actor ? 0 : 1)));
        int outX = 0, x = 0, skip = std::max(0, r.left);
        const int outY = zoom ? r.y / 2 + localRow / 2 : r.y + localRow;
        auto write = [&](uint8_t index, bool endpoint) {
            uint32_t& out = dst[outY * r.pitch + (zoom ? r.x / 2 : r.x) + outX];
            const uint32_t oldZ = out & 0xffff0000u;
            if (oldZ > z) {
                if (kind == DepthKind::Keyline && full && endpoint) out = keyline;
                return;
            }
            uint32_t newZ = z;
            // Actor and non-zoom Type12 blends retain background depth. Actor ZoomOut
            // also retains it for opaque pixels after the slant crosses zero.
            if (!lit && kind != DepthKind::Keyline && ((!zoom && (index & 0xc0)) || (zoom && actor && ((index & 0xc0) || slant >= 0))))
                newZ = oldZ;
            out = newZ | Blend(index, static_cast<uint16_t>(out), palette, masks);
            if (lit) light[outY * lightPitch + (zoom ? r.x / 2 : r.x) + outX] = full && !zoom && endpoint ? 4 : 2;
        };
        while (x < r.width) {
            const int run = static_cast<int8_t>(*rle++);
            if (run <= 0) {
                int length = -run;
                if (row < firstRow) {
                    x += length;
                    continue;
                }
                const int clipped = std::min(skip, length);
                skip -= clipped;
                x += clipped;
                length -= clipped;
                if (!zoom) {
                    if (r.left > 0 || full || x < r.right) outX += length;
                } else if (type12) {
                    if (drawRow && (r.left > 0 || full || x < r.right)) outX += length / 2 + (full ? 0 : (x & 1));
                } else if (full && actor) {
                    if (drawRow) outX += (length + (x & 1)) / 2;
                } else if (r.left > 0 || full || x < r.right) {
                    outX += length / 2;
                }
                x += length;
                continue;
            }
            int parity = x & 1;
            for (int i = 0; i < run; ++i, ++x) {
                const uint8_t index = *rle++;
                if (row < firstRow) continue;
                if (skip > 0) {
                    --skip;
                    continue;
                }
                const bool inRight = full || (r.left > 0 && !(!zoom && actor)) || x < r.right;
                if (!inRight) continue;
                bool sample = drawRow;
                if (zoom) {
                    if (type12 && full)
                        sample = drawRow && (i % 2 == 0);
                    else if (type12 || (full && actor))
                        sample = drawRow && (x & 1);
                    else {
                        sample = drawRow && parity;
                        if (drawRow) parity ^= 1;
                    }
                }
                if (sample) write(index, i == 0 || (run > 1 && i == run - 1));
                if (!zoom || sample) ++outX;
            }
        }
    }
}

void Bitmap(uint32_t* dst, const uint8_t* src, const uint16_t* palette, BitmapRect r, bool keyed, uint16_t colorMask)
{
    for (int y = r.height - 1; y >= 0; --y) {
        for (int x = 0; x < r.width; ++x) {
            const uint8_t index = src[(r.srcY + y) * r.srcPitch + r.srcX + x];
            if (keyed && index == 0) continue;
            uint32_t& out = dst[(r.dstY + y) * r.dstPitch + r.dstX + x];
            out = (out & 0xffff0000u) | (palette[index] & colorMask);
        }
    }
}

void Bitmap(uint16_t* dst, const uint8_t* src, const uint16_t* palette, BitmapRect r)
{
    for (int y = r.height - 1; y >= 0; --y) {
        for (int x = 0; x < r.width; ++x) {
            const uint8_t index = src[(r.srcY + y) * r.srcPitch + r.srcX + x];
            if (index != 0) dst[y * (static_cast<unsigned>(r.dstPitch) >> 1) + r.dstX + x] = palette[index];
        }
    }
}

void Ramp(uint32_t* dst, const uint8_t* rle, const uint8_t* depthFrame, const uint16_t* palette, RleRect r, uint32_t depth,
        float depthPerPixel, Masks masks, bool ghost, bool zoom)
{
    const bool full = r.left <= 0 && r.top <= 0 && r.width <= r.right && r.height <= r.bottom;
    const int first = std::max(0, r.top), rows = full ? r.height : r.bottom - r.top;
    if (rows <= 0 || r.width <= 0) return;
    // RampZoom aligns an odd top clip by shifting the destination origin up.
    const int originY = zoom ? (r.y - (!ghost && (r.top & 1) ? 1 : 0)) / 2 : r.y;
    for (int row = 0; row < first + rows; ++row) {
        const int local = row - first;
        const bool draw = row >= first && (!zoom || ((ghost ? local : full ? row + r.top : row) & 1));
        int x = 0, outX = 0, skip = std::max(0, r.left);
        const int y = zoom ? originY + local / 2 : originY + local;
        while (x < r.width) {
            const int run = static_cast<int8_t>(*rle++);
            ++depthFrame;
            if (run <= 0) {
                for (int i = 0; i < -run; ++i, ++x) {
                    if (row < first) continue;
                    if (skip > 0) {
                        --skip;
                        continue;
                    }
                    // Left-clipped transparent runs keep advancing beyond right.
                    if ((!zoom || (draw && (x & 1))) && (r.left > 0 || full || x < r.right)) ++outX;
                }
            } else {
                for (int i = 0; i < run; ++i, ++x) {
                    const uint8_t index = *rle++, sample = *depthFrame++;
                    if (row < first) continue;
                    if (skip > 0) {
                        --skip;
                        continue;
                    }
                    if ((!full && x >= r.right) || !draw || (zoom && !(x & 1))) continue;
                    uint32_t& out = dst[y * r.pitch + (zoom ? r.x / 2 : r.x) + outX++];
                    // The game multiplies in double without rounding to float, then the result is truncated.
                    const auto delta =
                            static_cast<int64_t>((static_cast<double>(sample) + (full ? 0 : 1)) * static_cast<double>(depthPerPixel));
                    const uint32_t z = (depth + static_cast<uint32_t>(delta)) << 16;
                    if ((out & 0xffff0000u) <= z) {
                        const uint16_t color = ghost
                                ? static_cast<uint16_t>(((out & masks.half) >> 1) + ((palette[index] & masks.half) >> 1))
                                : palette[index];
                        out = z | color;
                    }
                }
            }
        }
    }
}

void Shadow(uint32_t* dst, const uint8_t* runs, RleRect r, uint32_t depth, Masks masks, bool zoom)
{
    const bool full = r.left <= 0 && r.top <= 0 && r.width <= r.right && r.height <= r.bottom;
    const int first = std::max(0, r.top), rows = full ? r.height : r.bottom - r.top;
    if (rows <= 0 || r.width <= 0) return;
    uint32_t z = (depth - r.height * 16u) << 16;
    for (int row = 0; row < first + rows; ++row) {
        z += 0x100000u;
        const int local = row - first;
        int x = 0, outX = 0, skip = std::max(0, r.left);
        const bool draw = row >= first && (!zoom || (local & 1));
        const int y = zoom ? r.y / 2 + local / 2 : r.y + local;
        while (x < r.width) {
            const int run = static_cast<int8_t>(*runs++);
            if (row < first) {
                x += std::abs(run);
                continue;
            }
            int remainder = run;
            if (run > 0) {
                const int n = zoom ? (run + 1) / 2 : run;
                remainder = run - n * (zoom ? 2 : 1);
                for (int i = 0; i < n; ++i, x += zoom ? 2 : 1) {
                    if (skip > 0) {
                        skip -= zoom ? 2 : 1;
                        continue;
                    }
                    if (draw && (full || x < r.right)) {
                        uint32_t& out = dst[y * r.pitch + (zoom ? r.x / 2 : r.x) + outX++];
                        if ((out & 0xffff0000u) <= z) out = z | ((out & masks.half) >> 1);
                    }
                }
            }
            int length = -remainder;
            while (skip > 0 && length > 0) {
                --skip;
                --length;
                ++x;
            }
            const bool advance = r.left > 0 || (full ? draw : x < r.right);
            if (advance) outX += zoom ? length / 2 : length;
            x += length;
        }
    }
}

void Water(uint32_t* dst, const uint32_t* src, const uint8_t* mask, WaterRect r, Masks masks, WaterColors colors, bool halfRes)
{
    if (!halfRes) {
        r.srcPitch &= 0xffff;
        r.srcX &= 0xffff;
        r.srcY &= 0xffff;
        r.width &= 0xffff;
        r.height &= 0xffff;
    }
    if ((!mask && !src) || !r.width || !r.height) return;
    const uint32_t rows = halfRes ? r.height / 2 : r.height;
    // The unmasked full copy traverses bottom-up, which matters for overlapping buffers.
    for (uint32_t i = 0; i < rows; ++i) {
        const uint32_t y = !mask && !halfRes ? rows - i - 1 : i;
        const uint32_t sy = r.srcY + y * (halfRes ? 2 : 1);
        const int dy = (halfRes ? static_cast<uint32_t>(r.dstY) >> 1 : r.dstY) + y;
        if (!mask && !halfRes) {
            // Match MMX: load both pixels before either store, including when
            // destination is one pixel ahead of source in the same allocation.
            const uint32_t* in = src + sy * r.srcPitch + r.srcX;
            uint32_t* out = dst + dy * r.dstPitch + r.dstX;
            uint32_t x = 0;
            for (; x + 1 < r.width; x += 2) {
                const uint32_t a = in[x], b = in[x + 1];
                out[x] = a;
                out[x + 1] = b;
            }
            if (x < r.width) out[x] = in[x];
            continue;
        }
        for (uint32_t x = 0; x < r.width; x += halfRes ? 2 : 1) {
            const auto offset = sy * r.srcPitch + r.srcX + x;
            uint32_t value = src[offset];
            if (mask && !(value & 0x10000)) {
                uint32_t color = value & 0xffff;
                switch (mask[offset]) {
                case 1: color = ((color & masks.half) >> 1) + ((color & masks.quarter) >> 2) + colors.quarterWhite; break;
                case 2: color = ((color & masks.half) >> 1) + colors.halfWhite; break;
                case 3: color = ((color & masks.quarter) >> 2) + colors.threeQuarterWhite; break;
                case 4: color = 0xffff; break;
                }
                value = (halfRes ? 0 : value & 0xffff0000u) | (color & 0xffff);
            }
            const int dx = (halfRes ? static_cast<uint32_t>(r.dstX) >> 1 : r.dstX) + (halfRes ? x / 2 : x);
            dst[dy * r.dstPitch + dx] = value;
        }
    }
}

} // namespace obl::blit
