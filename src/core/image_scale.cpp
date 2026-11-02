#include "core/image_scale.h"

#include <cmath>
#include <vector>

namespace obl::image {
namespace {

struct Channels {
    int r, g, b;
};

// The scaler splits a pixel into red (high 5 bits), green (the 5 bits below it, i.e. bits 6..10 for 565 and 5..9 for 555)
// and blue (low 5 bits).
Channels Split(uint16_t pixel, bool is555)
{
    return is555 ? Channels{(pixel >> 10) & 0x1f, (pixel >> 5) & 0x1f, pixel & 0x1f}
                 : Channels{(pixel >> 11) & 0x1f, (pixel >> 6) & 0x1f, pixel & 0x1f};
}

// One bilinear step: a*(1-w) + b*w, stored as float, then converted with the current rounding mode (truncating store).
int Mix(int a, int b, float inverseWeight, float weight)
{
    float sum = static_cast<float>(static_cast<double>(a) * inverseWeight + static_cast<double>(b) * weight);
    return static_cast<int>(std::lrintf(sum));
}

} // namespace

void Resample16(const uint16_t* src, int srcW, int srcH, uint16_t* dst, int dstW, int dstH, bool is555)
{
    if (dstW <= 0 || dstH <= 0 || srcW < 2 || srcH < 2) return; // a source of one row/column would read outside the image
    float scaleX = static_cast<float>(static_cast<double>(srcW - 1) / dstW);
    float scaleY = static_cast<float>(static_cast<double>(srcH - 1) / dstH);
    for (int y = 0; y < dstH; ++y) {
        double sourceY = static_cast<double>(y) * scaleY;
        int iy = static_cast<int>(sourceY); // truncates toward zero
        float wy = static_cast<float>(sourceY - iy);
        float iwy = static_cast<float>(1.0 - wy);
        const uint16_t* row0 = src + static_cast<ptrdiff_t>(iy) * srcW;
        const uint16_t* row1 = row0 + srcW;
        for (int x = 0; x < dstW; ++x) {
            double sourceX = static_cast<double>(x) * scaleX;
            int ix = static_cast<int>(sourceX);
            float wx = static_cast<float>(sourceX - ix);
            float iwx = static_cast<float>(1.0 - wx);
            Channels p00 = Split(row0[ix], is555), p01 = Split(row0[ix + 1], is555);
            Channels p10 = Split(row1[ix], is555), p11 = Split(row1[ix + 1], is555);
            // Vertical pass on both columns, then the horizontal pass.
            int r = Mix(Mix(p00.r, p10.r, iwy, wy), Mix(p01.r, p11.r, iwy, wy), iwx, wx);
            int g = Mix(Mix(p00.g, p10.g, iwy, wy), Mix(p01.g, p11.g, iwy, wy), iwx, wx);
            int b = Mix(Mix(p00.b, p10.b, iwy, wy), Mix(p01.b, p11.b, iwy, wy), iwx, wx);
            if (r > 0x1f) r = 0x1f;
            if (g > (is555 ? 0x1f : 0x3f)) g = is555 ? 0x1f : 0x3f;
            if (b > 0x1f) b = 0x1f;
            dst[static_cast<ptrdiff_t>(y) * dstW + x] =
                    static_cast<uint16_t>(is555 ? (r * 0x20 + g) * 0x20 + b : (r * 0x20 + g) * 0x40 + b);
        }
    }
}

void BuildHalfSizeChain(const uint16_t* src, int srcW, int srcH, uint16_t* dst, int dstW, int dstH, bool is555)
{
    const uint16_t* current = src;
    int currentW = srcW, currentH = srcH;
    std::vector<uint16_t> previous, next;
    while (dstW <= currentW / 2) {
        int nextW = currentW / 2, nextH = currentH / 2;
        next.assign(static_cast<size_t>(nextW) * static_cast<size_t>(nextH), 0);
        Resample16(current, currentW, currentH, next.data(), nextW, nextH, is555);
        previous.swap(next);
        current = previous.data();
        currentW = nextW;
        currentH = nextH;
    }
    Resample16(current, currentW, currentH, dst, dstW, dstH, is555);
}

void BuildPalette16(const uint8_t* palette, bool is555, uint16_t* out)
{
    for (int i = 0; i < 256; ++i) {
        unsigned r = palette[i * 3] & 0xf8u, g = palette[i * 3 + 1], b = palette[i * 3 + 2] >> 3;
        out[i] = static_cast<uint16_t>(is555 ? ((r << 5 | (g & 0xf8u)) << 2) | b : (((g & 0xfcu) | r << 5) << 3) | b);
    }
}

} // namespace obl::image
