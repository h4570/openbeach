#include "core/text16.h"
#include "game/ui/renderer_api.h"
#include "game/ui/text_renderer.h"
#include "game/game/game_accessors.h"
#include "game/ui/text_encoding.h"
#include "game/ui/text_observer.h"
#include <vector>
#include <cwchar>
#include "game/ui/renderer_types.h"
#include "game/ui/font.h"
#include "game/ui/renderer_ops.h"
#include <cstring>

namespace obl::ui {
namespace {
using ui::BitmapFont;
using ui::Renderer;
using ui::TextRenderer;

uint16_t Color(const TextRenderer* r)
{
    return static_cast<uint16_t>(renderer_ops::PackColor(
            game::GetRenderer()->m_rgb555 != 0, (r->m_color >> 16) & 255, (r->m_color >> 8) & 255, r->m_color & 255));
}
void DrawGlyph(TextRenderer* r, const uint8_t* src, int srcPitch, void* dest, int destPitch, int sx, int sy, int dx, int dy, int width,
        int height, int bytes)
{
    text_renderer::BlitGlyph(*r, src, srcPitch, dest, destPitch, sx, sy, dx, dy, width, height, bytes, Color(r));
}
void DrawTextLine(TextRenderer* r, int x, int y, const Char16* text)
{
    auto* renderer = game::GetRenderer();
    const auto& font = *r->m_font;
    const auto& atlas = *font.m_image;
    if (r->m_align == 1 || r->m_align == 2) {
        const uint32_t extra = static_cast<uint32_t>(r->m_width) - text_renderer::Measure(font, text);
        x += static_cast<int32_t>(r->m_align == 2 ? extra >> 1 : extra);
    }
    const int left = renderer->m_view[0], top = renderer->m_view[1];
    const int right = left + renderer->m_view[2], bottom = top + renderer->m_view[3];
    void* dest = renderer->m_workBuffer;
    int pitch = renderer->m_width, ox = 0, oy = 0;
    const bool direct = renderer->m_directMode != 0;
    if (direct) {
        dest = obl::ui::renderer::GetBackBufferBits(renderer);
        if (!dest) return;
        const auto* surface = ui::SurfaceAt(*renderer, renderer->m_activeSurface);
        pitch = surface->m_pitch / 2;
        ox = surface->m_originX;
        oy = surface->m_originY;
    }
    for (; *text; ++text) {
        const auto& glyph = font.m_glyphs[*text];
        int dx = r->m_x + x - ox, dy = r->m_y + y - oy;
        int sx = glyph.left, sy = glyph.top, ex = glyph.right, ey = glyph.bottom;
        if (dx <= right && dy <= bottom && dx + ex - sx >= left && dy + ey - sy >= top) {
            if (dx < left) {
                sx += left - dx;
                dx = left;
            }
            if (dx + ex - sx > right) ex = sx - dx + right;
            if (dy < top) {
                sy += top - dy;
                dy = top;
            }
            if (dy + ey - sy > bottom) ey = sy - dy + bottom;
            DrawGlyph(r, atlas.m_pixels, atlas.m_width, dest, pitch, sx, sy, dx, dy, ex - sx, ey - sy, direct ? 2 : 4);
        }
        x += glyph.right - glyph.left;
        if (text[1]) x += font.m_kerning[*text * 255 + text[1]];
    }
    if (direct) obl::ui::renderer::UnlockSurface(renderer);
}
void DrawTextWrapped(TextRenderer* r, int x, int y, const Char16* text)
{
    text_renderer::DrawWrapped(*r, x, y, text, [&](int dx, int dy, const Char16* part) { DrawTextLine(r, dx, dy, part); });
}

} // namespace
namespace font_text {
void* Construct(void* textRenderer, void* font, void* table)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    std::memcpy(&r->m_x, game::GetRenderer()->m_view, sizeof(game::GetRenderer()->m_view));
    r->m_color = 0xffffffff;
    r->m_outlined = 0;
    r->m_align = 0;
    r->m_font = static_cast<BitmapFont*>(font);
    r->m_textTable = table;
    return r;
}
void SetRect(void* textRenderer, int x, int y, int w, int h)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    r->m_x = x;
    r->m_y = y;
    r->m_width = w;
    r->m_height = h;
}
void SetColor(void* textRenderer, uint32_t color)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    r->m_color = color;
}
void SetOutlined(void* textRenderer, uint32_t outline)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    r->m_outlined = static_cast<uint8_t>(outline);
}
void SetAlign(void* textRenderer, int align)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    r->m_align = align;
}
int MeasureWidth(const void* textRenderer, const Char16* text)
{
    auto* r = static_cast<const TextRenderer*>(textRenderer);

    Text16 encoded;
    text = EncodedText(r, text, encoded);
    return static_cast<int32_t>(text_renderer::Measure(*r->m_font, text));
}
int CountLines(const void* textRenderer, const Char16* text, uint32_t width, uint32_t x)
{
    auto* r = static_cast<const TextRenderer*>(textRenderer);

    Text16 encoded;
    text = EncodedText(r, text, encoded);
    return text_renderer::CountLines(*r->m_font, text, width, x);
}
uint32_t MeasureWrappedWidth(const void* textRenderer, const Char16* text, uint32_t width, uint32_t x)
{
    auto* r = static_cast<const TextRenderer*>(textRenderer);

    Text16 encoded;
    text = EncodedText(r, text, encoded);
    return text_renderer::WrappedWidth(*r->m_font, text, width, x);
}
void TruncateWithEllipsis(void* textRenderer, Char16* text, int width)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    Text16 encoded;
    if (EncodedText(r, text, encoded) == text) {
        text_renderer::Truncate(*r->m_font, text, width);
        return;
    }
    std::vector<Char16> buffer(encoded.size() + 8, u'\0');
    std::char_traits<obl::Char16>::copy(buffer.data(), encoded.c_str(), encoded.size());
    text_renderer::Truncate(*r->m_font, buffer.data(), width);
    text16::Copy(text, buffer.data());
}
void DrawLine(void* textRenderer, int x, int y, const Char16* text)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    NotifyTextDrawLine(r, x, y, text);
    Text16 encoded;
    DrawTextLine(r, x, y, EncodedText(r, text, encoded));
}
void DrawWrapped(void* textRenderer, int x, int y, const Char16* text)
{
    auto* r = static_cast<TextRenderer*>(textRenderer);

    NotifyTextDrawWrapped(r, x, y, text);
    TextDrawWrappedScope wrapped;
    Text16 encoded;
    DrawTextWrapped(r, x, y, EncodedText(r, text, encoded));
}
} // namespace font_text
} // namespace obl::ui
