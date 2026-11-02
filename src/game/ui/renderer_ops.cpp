#include "game/ui/renderer_ops.h"

namespace obl::renderer_ops {

uint16_t PackColor(bool rgb555, uint32_t r, uint32_t g, uint32_t b)
{
    const uint32_t blue = (b >> 3) & 0x1f;
    if (rgb555) return static_cast<uint16_t>(((((r & 0xf8) << 5) | (g & 0xf8)) << 2) | blue);
    return static_cast<uint16_t>(((((r & 0xfff8) << 5) | (g & 0xfc)) << 3) | blue);
}

bool ZoomScale(int level, float& scale)
{
    switch (level) {
    case 0: scale = 2.0f; return true;
    case 1: scale = 1.0f; return true;
    case 2: scale = 0.5f; return true;
    default: return false;
    }
}

bool IsRectVisible(const VisibleView& view, int x, int y, int srcX, int srcY, uint32_t width, uint32_t height, int strayEdge)
{
    int edgeX = view.x + view.width;
    int edgeY = view.y + view.height;
    if (view.zoomLevel != 1 && !(view.directMode && !view.overlay)) {
        if (view.zoomLevel == 2) {
            edgeX = view.x + view.width * 2;
            edgeY = view.y + view.height * 2;
        } else if (view.zoomLevel == 0) {
            edgeX = static_cast<int>(static_cast<uint32_t>(view.width) >> 1) + view.x;
            edgeY = static_cast<int>(static_cast<uint32_t>(view.height) >> 1) + view.y;
        } else {
            edgeX = edgeY = strayEdge;
        }
    }
    if (edgeX < x || edgeY < y) return false;
    const int right = static_cast<int>(width - static_cast<uint32_t>(srcX)) + x;
    const int bottom = static_cast<int>(height - static_cast<uint32_t>(srcY)) + y;
    return view.x <= right && view.y <= bottom;
}

bool IntersectRects(const Rect& a, const Rect& b, Rect& out)
{
    if (a.right < b.left || b.right < a.left || a.bottom < b.top || b.bottom < a.top) return false;
    out.left = a.left > b.left ? a.left : b.left;
    out.top = a.top > b.top ? a.top : b.top;
    out.right = b.right <= a.right ? b.right : a.right;
    out.bottom = b.bottom <= a.bottom ? b.bottom : a.bottom;
    return true;
}

ClipResult ClipRect(const ClipView& view, int& x, int& y, int& left, int& top, int& right, int& bottom)
{
    ClipResult result;
    // The far edge of the view in destination coordinates.
    int edgeX = view.x + view.width;
    int edgeY = view.y + view.height;
    if (!view.directMode && view.zoomLevel != 1) {
        if (view.zoomLevel == 2) {
            edgeX = view.x + view.width * 2;
            edgeY = view.y + view.height * 2;
        } else if (view.zoomLevel == 0) {
            edgeX = static_cast<int>(static_cast<uint32_t>(view.width) >> 1) + view.x;
            edgeY = static_cast<int>(static_cast<uint32_t>(view.height) >> 1) + view.y;
        }
    }
    if (x > edgeX || y > edgeY) return result;
    if ((right - left) + x < view.x || (y - top) + bottom < view.y) return result;

    if (x < view.x) {
        const int shift = view.x - x;
        x += shift;
        left += shift;
    }
    if (edgeX < (right - left) + x) right = (left - x) + edgeX;
    if (y < view.y) {
        const int shift = view.y - y;
        y += shift;
        top += shift;
    }
    if (edgeY < (y - top) + bottom) bottom = (top - y) + edgeY;

    result.visible = true;
    if (view.recordDirty) {
        result.addDirty = true;
        result.dirty = {x, y, (right - left) + x, (y - top) + bottom};
    }
    return result;
}

FillSpan ClipFill(int sceneWidth, int sceneHeight, const Rect& rect)
{
    FillSpan span{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    if (span.y < 0) {
        span.height += span.y;
        span.y = 0;
    }
    if (span.x < 0) {
        span.width += span.x;
        span.x = 0;
    }
    if (sceneWidth < span.width + span.x) span.width = sceneWidth - span.x;
    if (sceneHeight < span.height + span.y) span.height = sceneHeight - span.y;
    return span;
}

void FillScene(uint32_t* scene, int sceneWidth, const FillSpan& span, uint16_t color)
{
    if (span.width <= 0 || span.height <= 0) return;
    uint32_t* row = scene + static_cast<ptrdiff_t>(sceneWidth) * span.y + span.x;
    for (int line = 0; line < span.height; ++line, row += sceneWidth)
        for (int column = 0; column < span.width; ++column) row[column] = (row[column] & 0xffff0000u) + color;
}

void FillBackBuffer(uint16_t* rowStart, uint32_t pitchPixels, const FillSpan& span, uint16_t color)
{
    if (span.width <= 0 || span.height <= 0) return;
    for (int line = 0; line < span.height; ++line, rowStart += pitchPixels)
        for (int column = 0; column < span.width; ++column) rowStart[column] = color;
}

void ApplyFade(uint16_t* pixels, int width, int rows, uint8_t mode, blit::Masks masks, blit::WaterColors colors)
{
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < width; ++column, ++pixels) {
            const uint32_t c = *pixels;
            const uint32_t half = (masks.half & c) >> 1;
            const uint32_t quarter = (masks.quarter & c) >> 2;
            switch (mode) {
            case 1: *pixels = static_cast<uint16_t>(half + colors.quarterWhite + quarter); break;
            case 2: *pixels = static_cast<uint16_t>(half + colors.halfWhite); break;
            case 3: *pixels = static_cast<uint16_t>(quarter + colors.threeQuarterWhite); break;
            case 4: *pixels = 0xffff; break;
            default: break;
            }
        }
    }
}

void ClearRows(uint16_t* bits, size_t count, uint16_t key)
{
    for (size_t i = 0; i < count; ++i) bits[i] = key;
}

void CopyKeyed(const uint16_t* src, ptrdiff_t srcStride, uint16_t* dst, ptrdiff_t dstStride, uint32_t width, int rows, uint16_t key)
{
    for (int row = 0; row < rows; ++row, src += srcStride, dst += dstStride) {
        for (uint32_t column = 0; column < width; ++column) {
            if (src[column] != key) dst[column] = src[column];
        }
    }
}

int StepFade(ui::RendererFade& fade)
{
    if (!fade.m_active) return -1;
    if (fade.m_level == 0 && fade.m_hold != 0) --fade.m_hold;
    if (fade.m_holding) {
        if (--fade.m_hold == 0) fade.m_holding = 0;
        return 4;
    }
    const int mode = fade.m_level;
    if (!fade.m_reverse && fade.m_level == 4) {
        fade.m_hold = 20;
        fade.m_reverse = 1;
        fade.m_holding = 1;
    } else if (fade.m_reverse && fade.m_level == 0) {
        fade.m_active = 0;
        fade.m_reverse = 0;
    } else if (fade.m_tick++ >= 2) {
        fade.m_level += fade.m_reverse ? -1 : 1;
        fade.m_tick = 0;
    }
    return mode;
}

} // namespace obl::renderer_ops
