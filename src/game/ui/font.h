#pragma once
#include <cstdint>

namespace obl::ui {
struct RendererImage; // game/ui/renderer_types.h
// The atlas is a renderer image; its allocation and surface locking stay with the renderer backend.
struct GlyphRect {
    uint16_t left, top, right, bottom;
};
struct BitmapFont {
    RendererImage* m_image;
    GlyphRect m_glyphs[256];
    int8_t m_kerning[65536];
    uint32_t m_size;
    uint32_t m_bold;
};
struct GdiFont {
    RendererImage* m_image;
    GlyphRect m_glyphs[0x80]; // only 0x20..0x7e are written
    int8_t m_kerning[0x80 * 0x80]; // [first * 0x80 + second] = kerning amount - 1, 0 for pairs without a kerning entry
    uint32_t m_size; // point size
    uint32_t m_flags; // bit 0 bold, bit 1 italic
};
struct TextRenderer {
    int32_t m_x;
    int32_t m_y;
    int32_t m_width;
    int32_t m_height;
    uint32_t m_color; // RGB passed to SetColor
    uint8_t m_outlined;
    int32_t m_align; // 0 left, 1 right, 2 centre
    BitmapFont* m_font;
    void* m_textTable;
};
} // namespace obl::ui
