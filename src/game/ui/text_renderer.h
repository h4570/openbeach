#pragma once

#include "core/text16.h"
#include "game/ui/font.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>
#include "core/char16.h"
#include <cstdint>

namespace obl::text_renderer {
// The metrics API truncates each advance to 16 bits; drawing adds signed kerning instead.
inline uint32_t Advance(const ui::BitmapFont& font, const Char16* text)
{
    const auto& g = font.m_glyphs[*text];
    int width = g.right - g.left;
    if (text[1]) width += font.m_kerning[*text * 255 + text[1]];
    return static_cast<uint16_t>(width);
}
inline uint32_t Measure(const ui::BitmapFont& font, const Char16* text)
{
    uint32_t width = 0;
    for (; *text; ++text) width += Advance(font, text);
    return width;
}

// CountLines and MeasureWrappedWidth do not interpret '~'. The line draw wrapper does.
template<class Emit> void WrapMetrics(const ui::BitmapFont& font, const Char16* text, uint32_t width, uint32_t startX, Emit emit)
{
    const Char16* line = text;
    const Char16* word = text;
    const Char16* p = text;
    uint32_t consumed = 0, inWord = 0, x = startX;
    while (*p) {
        x += Advance(font, p);
        if (p != text && *p != u' ' && p[-1] == u' ') {
            inWord = 0;
            word = p;
        }
        if (*p != u' ') ++inWord;
        if (x > width && inWord < consumed && word != line) {
            emit(line, consumed - inWord);
            line = p = word;
            consumed = inWord = 0;
            x = startX;
        } else {
            ++p;
            ++consumed;
        }
    }
    emit(line, static_cast<uint32_t>(p - line));
}
inline int CountLines(const ui::BitmapFont& font, const Char16* text, uint32_t width, uint32_t startX)
{
    int count = 0;
    WrapMetrics(font, text, width, startX, [&](const Char16*, uint32_t) { ++count; });
    return count;
}
inline uint32_t WrappedWidth(const ui::BitmapFont& font, const Char16* text, uint32_t width, uint32_t startX)
{
    uint32_t maximum = 0;
    WrapMetrics(font, text, width, startX, [&](const Char16* p, uint32_t n) {
        const Text16 line(p, n);
        maximum = std::max(maximum, Measure(font, line.c_str()));
    });
    return maximum;
}
inline void Truncate(const ui::BitmapFont& font, Char16* text, int width)
{
    if (static_cast<int32_t>(Measure(font, text)) <= width) return;
    size_t n = text16::Length(text);
    n = n > 1 ? n - 2 : 0;
    for (;;) {
        text16::Copy(text + n, u"...");
        if (static_cast<int32_t>(Measure(font, text)) <= width || n == 0) return;
        --n;
    }
}

template<class Emit> void DrawWrapped(const ui::TextRenderer& r, int x, int y, const Char16* text, Emit emit)
{
    const auto& font = *r.m_font;
    const int descent = static_cast<int>(static_cast<double>(font.m_size) * -1.2000000476837158);
    if (r.m_height < static_cast<int>(font.m_size) - descent) {
        emit(x, y, text);
        return;
    }
    const Char16* line = text;
    const Char16* word = text;
    const Char16* p = text;
    uint32_t consumed = 0, inWord = 0;
    int currentX = x;
    while (*p) {
        currentX += Advance(font, p);
        bool forced = false;
        if (p != text) {
            if (*p != u' ' && p[-1] == u' ') {
                inWord = 0;
                word = p;
            }
            if (*p == u'~') {
                ++p;
                forced = true;
                inWord = 0;
                word = p;
            }
        }
        if (*p != u' ' && (p == text || p[-1] != u'~')) ++inWord;
        if ((r.m_width < currentX && inWord < consumed && word != line) || forced) {
            Text16 part(line, consumed - inWord);
            emit(x, y, part.c_str());
            y += 2 - descent;
            if (r.m_height < static_cast<int>(font.m_size) + y) return;
            line = p = word;
            consumed = inWord = 0;
            currentX = x;
        } else {
            ++consumed;
            ++p;
        }
    }
    emit(x, y, line);
}

inline void BlitGlyph(const ui::TextRenderer& r, const uint8_t* src, int srcPitch, void* dest, int destPitch, int sx, int sy, int dx,
        int dy, int width, int height, int bytesPerPixel, uint16_t color)
{
    for (int y = height - 1; y >= 0; --y) {
        const auto* row = src + (sy + y) * srcPitch + sx;
        auto* out = static_cast<uint8_t*>(dest) + ((dy + y) * destPitch + dx) * bytesPerPixel;
        for (int x = 0; x < width; ++x) {
            uint16_t value = color;
            bool write = (row[x] & 1) != 0;
            if (r.m_outlined) {
                value = 0xffff;
                if (row[x] & 2) {
                    value = 0;
                    write = true;
                }
            }
            if (write) std::memcpy(out + x * bytesPerPixel, &value, sizeof(value));
        }
    }
}
} // namespace obl::text_renderer

namespace obl::ui::font_text {
void* Construct(void* textRenderer, void* font, void* table);
void SetRect(void* textRenderer, int x, int y, int w, int h);
void SetColor(void* textRenderer, uint32_t color);
void SetOutlined(void* textRenderer, uint32_t outline);
void SetAlign(void* textRenderer, int align);
int MeasureWidth(const void* textRenderer, const Char16* text);
int CountLines(const void* textRenderer, const Char16* text, uint32_t width, uint32_t x);
uint32_t MeasureWrappedWidth(const void* textRenderer, const Char16* text, uint32_t width, uint32_t x);
void TruncateWithEllipsis(void* textRenderer, Char16* text, int width);
void DrawLine(void* textRenderer, int x, int y, const Char16* text);
void DrawWrapped(void* textRenderer, int x, int y, const Char16* text);
} // namespace obl::ui::font_text
