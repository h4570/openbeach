#pragma once

#include <cstdint>
#include "core/rect.h"

namespace obl::blit {

struct RleRect {
    int pitch, x, y, width, height, left, top, right, bottom;
};

struct Masks {
    uint16_t half, quarter;
};

// Source clip bounds are exclusive. The destination origin has already been clipped by the caller.
void Ui(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect rect);
void Ui(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect rect);
// Type2 retains depth and marks the first literal after each gap with the source width.
void Type2(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect rect);
// Byte RLE takes max(dst, int(sample)-24), without wrapping negative samples.
void RleToBytes(uint8_t* dst, const uint8_t* rle, RleRect rect, bool zoom);
// Positive controls clear bytes; there is no literal payload.
void RleClear(uint8_t* dst, const uint8_t* runs, RleRect rect);
void MaxBytes(uint8_t* dst, const uint8_t* src, RleRect rect);
void Translucent(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect rect, Masks masks);
void Translucent(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect rect, Masks masks);
uint16_t Blend(uint8_t index, uint16_t background, const uint16_t* palette, Masks masks);

// depthFrame follows encoded RLE offsets, including control bytes and literal payloads.
void Ramp(uint32_t* dst, const uint8_t* rle, const uint8_t* depthFrame, const uint16_t* palette, RleRect rect, uint32_t depth,
        float depthPerPixel, Masks masks, bool ghost, bool zoom);
// Shadow runs have no literal payload, unlike the palette-based RLE families.
void Shadow(uint32_t* dst, const uint8_t* runs, RleRect rect, uint32_t depth, Masks masks, bool zoom);

struct WaterColors {
    uint16_t quarterWhite, halfWhite, threeQuarterWhite;
};
struct WaterRect {
    uint32_t srcPitch, srcX, srcY, width, height;
    int dstPitch, dstX, dstY;
};
// Mask uses source pitch/origin; sampled bytes must be 0..4 (game animations emit 0/1).
// Source/destination may overlap: masked/HalfRes walk forward, unmasked Full
// walks bottom-up and loads pixel pairs before storing. Mask storage is separate.
// HalfRes drops source depth for water pixels only.
void Water(uint32_t* dst, const uint32_t* src, const uint8_t* mask, WaterRect rect, Masks masks, WaterColors colors, bool halfRes);

enum class DepthKind { Actor, Keyline, Type12, Lit };

// Light pitch is independent of color pitch. Zoom coordinates are still in source-scale pixels.
void Depth(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, RleRect rect, uint32_t depth, int slant, Masks masks, DepthKind kind,
        bool zoom = false, uint16_t keyline = 0, uint8_t* light = nullptr, int lightPitch = 0);

struct BitmapRect {
    int srcPitch, dstPitch, srcX, srcY, dstX, dstY, width, height;
};

void Bitmap(uint32_t* dst, const uint8_t* src, const uint16_t* palette, BitmapRect rect, bool keyed, uint16_t colorMask = 0xffff);
void Bitmap(uint16_t* dst, const uint8_t* src, const uint16_t* palette, BitmapRect rect);

} // namespace obl::blit

namespace obl::ui {
bool RegisterBlitDrivers();
// Pixel primitives; counters and renderer state are shared by all native callers.
namespace pixel_primitives {
void ActorSelect(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,
        int b, uint32_t z, int slant, char lit, char unused, char keyline);
void DepthLitSelect(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,
        int b, uint32_t z, int slant);
void TranslucentSelect(
        uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b);
void Type2(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b);
void RleToBytes8(uint8_t* dst, const uint8_t* src, uint32_t unused, int pitch, int x, int y, int w, int h, int l, int t, int r, int b);
void RleToBytes8ZoomOut(
        uint8_t* dst, const uint8_t* src, uint32_t unused, int pitch, int x, int y, int w, int h, int l, int t, int r, int b);
void MaxBytes8(uint8_t* dst, const uint8_t* src, uint32_t unusedArg2, int pitch, int x, int y, int sourcePitch, int unusedHeight, int l,
        int t, int r, int b);
void RleCopy32(uint8_t* dst, const uint8_t* src, int pitch, int x, int y, int w, int h, screen::Rect clip);
uint16_t GetMaskHalf();
uint16_t GetMaskQuarter();
void Ui16(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left, int top,
        int right, int bottom);
void Ui32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left, int top,
        int right, int bottom);
void Translucent16(uint16_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom);
void Translucent32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom);
void Bitmap8Keyed16(
        const uint8_t* src, int sp, uint16_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height, const uint16_t* palette);
void Bitmap8Opaque32(
        const uint8_t* src, int sp, uint32_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height, const uint16_t* palette);
void Bitmap8Keyed32(
        const uint8_t* src, int sp, uint32_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height, const uint16_t* palette);
void Bitmap8KeyedMasked32(const uint8_t* src, int sp, uint32_t* dst, int dp, int sx, int sy, int dx, int dy, int width, int height,
        const uint16_t* palette, uint16_t mask);
void Depth32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left, int top,
        int right, int bottom, uint32_t depth, int slant);
void DepthKeyline32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant);
void DepthZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant);
void DepthType12Unscaled32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height,
        int left, int top, int right, int bottom, uint32_t depth);
void DepthType12ZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height,
        int left, int top, int right, int bottom, uint32_t depth);
void DepthLit32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant);
void DepthLitZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int width, int height, int left,
        int top, int right, int bottom, uint32_t depth, int slant);
void DepthRamp32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y, int w, int h,
        int l, int t, int r, int b, uint32_t z, float slope);
void DepthRampGhost32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y, int w,
        int h, int l, int t, int r, int b, uint32_t z, float slope);
void DepthRampZoomOut32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y, int w,
        int h, int l, int t, int r, int b, uint32_t z, float slope);
void DepthRampGhostZoomOut32(uint32_t* dst, const uint8_t* rle, const uint8_t* frame, const uint16_t* palette, int pitch, int x, int y,
        int w, int h, int l, int t, int r, int b, uint32_t z, float slope);
void Shadow32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r, int b,
        uint32_t z);
void ShadowZoomOut32(uint32_t* dst, const uint8_t* rle, const uint16_t* palette, int pitch, int x, int y, int w, int h, int l, int t, int r,
        int b, uint32_t z);
void WaterHalfRes(const uint32_t* src, uint32_t sp, uint32_t* dst, int dp, uint32_t sx, uint32_t sy, int dx, int dy, uint32_t w, uint32_t h,
        const uint8_t* mask);
void WaterFullRes(const uint32_t* src, uint32_t sp, uint32_t* dst, int dp, uint32_t sx, uint32_t sy, int dx, int dy, uint16_t w, uint16_t h,
        const uint8_t* mask);
} // namespace pixel_primitives
} // namespace obl::ui
