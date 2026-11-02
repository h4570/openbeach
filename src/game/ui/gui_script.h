#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include "game/ui/gui_script_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"

namespace obl::gui_script {

struct Cursor {
    const char* text;
    size_t at = 0;
    // Game callers provide delimiter-terminated input; offline callers can bound truncated data.
    size_t end = std::numeric_limits<size_t>::max();

    bool Done() const { return at >= end || text[at] == '\0'; }
    char Peek() const { return Done() ? '\0' : text[at]; }
};

inline bool IsSpace(char c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}
inline void SkipWhitespace(Cursor& cursor)
{
    while (IsSpace(cursor.Peek())) ++cursor.at;
}

inline bool ReadToken(Cursor& cursor, std::string& token)
{
    SkipWhitespace(cursor);
    token.clear();
    while (!cursor.Done()) {
        char c = cursor.Peek();
        if (c == ')' || c == '\r' || c == ',' || c == '(' || c == '{') break;
        token += c; // LF, TAB, spaces and '/' inside a token are literal bytes.
        ++cursor.at;
    }
    if (cursor.Done()) return false;
    SkipWhitespace(cursor);
    if (cursor.Peek() == ',' || cursor.Peek() == ')') ++cursor.at;
    return true;
}

inline int32_t Signed(uint32_t value)
{
    return std::bit_cast<int32_t>(value);
}
inline int32_t Add(int32_t a, int32_t b)
{
    return Signed(uint32_t(a) + uint32_t(b));
}
inline int32_t Sub(int32_t a, int32_t b)
{
    return Signed(uint32_t(a) - uint32_t(b));
}

inline bool ReadDigits(Cursor& cursor, int32_t& value)
{
    uint32_t accumulator = 0;
    while (!cursor.Done()) {
        char c = cursor.Peek();
        if (c == ' ' || c == ',' || c == ')') break;
        // Each byte is sign-extended and the sum wraps in 32 bits: no digit validation and no signed-overflow UB.
        accumulator = accumulator * 10u + uint32_t(static_cast<int8_t>(c)) - 48u;
        ++cursor.at;
    }
    value = Signed(accumulator);
    return !cursor.Done();
}

inline bool ReadInt(Cursor& cursor, int32_t& value, bool consumeDelimiter)
{
    SkipWhitespace(cursor);
    if (!ReadDigits(cursor, value)) return false;
    SkipWhitespace(cursor);
    if (consumeDelimiter) {
        if (cursor.Peek() == ',' || cursor.Peek() == ')') ++cursor.at;
        SkipWhitespace(cursor);
    }
    return true;
}

inline bool ReadFloat(Cursor& cursor, float& value)
{
    SkipWhitespace(cursor);
    std::string token;
    while (!cursor.Done() && cursor.Peek() != ' ' && cursor.Peek() != ',' && cursor.Peek() != ')') {
        token += cursor.Peek();
        ++cursor.at;
    }
    if (cursor.Done()) return false;
    value = static_cast<float>(std::strtod(token.c_str(), nullptr));
    // Unlike ReadInt, a comma after spaces remains for the caller.
    if (cursor.Peek() == ',' || cursor.Peek() == ')') ++cursor.at;
    SkipWhitespace(cursor);
    return true;
}

struct Bounds {
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool operator==(const Bounds&) const = default;
};

enum class BoundsResult { Ok, Truncated, OutsideParent };

inline BoundsResult ReadBounds(Cursor& cursor, Bounds& bounds, int32_t width, int32_t height, const Bounds* parent = nullptr)
{
    SkipWhitespace(cursor);
    bool right = false, bottom = false, stretch = false, centerX = false, centerY = false;
    for (;;) {
        switch (cursor.Peek()) {
        case 'T':
        case 'L': break;
        case 'R': right = true; break;
        case 'B': bottom = true; break;
        case 'S': stretch = true; break;
        case 'C': centerX = true; break;
        case 'Y': centerY = true; break;
        default: goto coordinates;
        }
        ++cursor.at;
    }
coordinates:

    int32_t* values[] = {&bounds.x0, &bounds.y0, &bounds.x1, &bounds.y1};
    for (size_t i = 0; i < 4; ++i) {
        if (i != 0) SkipWhitespace(cursor);
        if (!ReadDigits(cursor, *values[i])) return BoundsResult::Truncated;
        SkipWhitespace(cursor);
        if (cursor.Peek() == ',') ++cursor.at; // ')' is intentionally left in place.
    }
    if (right) {
        bounds.x0 = Sub(width, bounds.x0);
        bounds.x1 = Sub(width, bounds.x1);
    }
    if (bottom) {
        bounds.y0 = Sub(height, bounds.y0);
        bounds.y1 = Sub(height, bounds.y1);
    }
    if (stretch) bounds.x1 = Sub(width, bounds.x1);
    if (centerX) {
        int32_t span = Sub(bounds.x1, bounds.x0);
        bounds.x0 = Signed((uint32_t(width) >> 1) - (uint32_t(span) >> 1));
        bounds.x1 = Add(bounds.x0, span);
    }
    if (centerY) {
        int32_t span = Sub(bounds.y1, bounds.y0);
        bounds.y0 = Signed((uint32_t(height) >> 1) - (uint32_t(span) >> 1));
        bounds.y1 = Add(bounds.y0, span);
    }
    if (parent && parent->x0 != parent->x1 && parent->y0 != parent->y1) {
        bounds.x0 = Add(bounds.x0, parent->x0);
        bounds.x1 = Add(bounds.x1, parent->x0);
        bounds.y0 = Add(bounds.y0, parent->y0);
        bounds.y1 = Add(bounds.y1, parent->y0);
        if (bounds.x1 > parent->x1 || bounds.y1 > parent->y1) return BoundsResult::OutsideParent;
    }
    return BoundsResult::Ok;
}

inline bool LooksLikeBounds(const std::string& text, size_t at)
{
    auto skipBlanks = [&] {
        while (at < text.size() && IsSpace(text[at])) ++at;
    };
    skipBlanks();
    while (at < text.size() && std::string_view("TBLRSCY").find(text[at]) != std::string_view::npos) ++at;
    for (int coordinate = 0; coordinate < 4; ++coordinate) {
        skipBlanks();
        size_t digits = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        if (at == digits) return false;
        skipBlanks();
        if (at >= text.size()) return false;
        if (coordinate < 3 && text[at] != ',') return false;
        if (coordinate == 3) return text[at] == ',' || text[at] == ')';
        ++at;
    }
    return false;
}

} // namespace obl::gui_script

namespace obl::gui {
// The parser's token, integer, float and bounds readers: the parser files call them directly.
void GuiScriptReadToken(ui::GuiScript& script, char* out);
void GuiScriptReadInt(ui::GuiScript& script, int32_t& out, bool consumeDelimiter);
void GuiScriptReadFloat(ui::GuiScript& script, float& out);
void GuiScriptReadBounds(ui::GuiScript& script, gui_script::Bounds& out, ui::GuiWidget* parent);
} // namespace obl::gui
