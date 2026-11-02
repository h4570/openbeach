#include "core/code_page.h"

#include <cstdint>

namespace obl::code_page {
namespace {

// Windows-1252 bytes 0x80..0x9f; the unassigned ones stay at their C1 code point.
constexpr char16_t kHigh[32] = {
        0x20ac,
        0x0081,
        0x201a,
        0x0192,
        0x201e,
        0x2026,
        0x2020,
        0x2021,
        0x02c6,
        0x2030,
        0x0160,
        0x2039,
        0x0152,
        0x008d,
        0x017d,
        0x008f,
        0x0090,
        0x2018,
        0x2019,
        0x201c,
        0x201d,
        0x2022,
        0x2013,
        0x2014,
        0x02dc,
        0x2122,
        0x0161,
        0x203a,
        0x0153,
        0x009d,
        0x017e,
        0x0178,
};

} // namespace

char16_t WidenByte(unsigned char byte)
{
    return byte >= 0x80 && byte < 0xa0 ? kHigh[byte - 0x80] : static_cast<char16_t>(byte);
}

char NarrowUnit(char16_t unit, bool* mapped)
{
    if (mapped) *mapped = true;
    if (unit < 0x80 || (unit >= 0xa0 && unit <= 0xff)) return static_cast<char>(unit);
    for (int i = 0; i < 32; ++i)
        if (kHigh[i] == unit) return static_cast<char>(0x80 + i);
    if (mapped) *mapped = false;
    return '?';
}

size_t Widen(const char* text, char16_t* out, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        out[i] = WidenByte(static_cast<unsigned char>(text[i]));
        if (!text[i]) return i + 1;
    }
    if (capacity) out[capacity - 1] = 0;
    return 0;
}

size_t Narrow(const char16_t* text, char* out, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        out[i] = NarrowUnit(text[i]);
        if (!text[i]) return i + 1;
    }
    if (capacity) out[capacity - 1] = 0;
    return 0;
}

std::u16string Widen(std::string_view text)
{
    std::u16string out;
    out.reserve(text.size());
    for (char c : text) out += WidenByte(static_cast<unsigned char>(c));
    return out;
}

std::string Narrow(std::u16string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char16_t unit : text) out += NarrowUnit(unit);
    return out;
}

std::string Utf16ToUtf8(std::u16string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t code = text[i];
        if (code >= 0xd800 && code < 0xdc00 && i + 1 < text.size() && text[i + 1] >= 0xdc00 && text[i + 1] < 0xe000)
            code = 0x10000 + ((code - 0xd800) << 10) + (text[++i] - 0xdc00);
        else if (code >= 0xd800 && code < 0xe000)
            code = 0xfffd;
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | code >> 6);
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | code >> 12);
            out += static_cast<char>(0x80 | (code >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | code >> 18);
            out += static_cast<char>(0x80 | (code >> 12 & 0x3f));
            out += static_cast<char>(0x80 | (code >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return out;
}

std::u16string Utf8ToUtf16(std::string_view text)
{
    std::u16string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const int extra = lead < 0x80 ? 0 : (lead & 0xe0) == 0xc0 ? 1 : (lead & 0xf0) == 0xe0 ? 2 : (lead & 0xf8) == 0xf0 ? 3 : -1;
        uint32_t code = extra == 0 ? lead : extra == 1 ? lead & 0x1f : extra == 2 ? lead & 0x0f : lead & 0x07;
        bool valid = extra >= 0 && i + extra < text.size();
        for (int k = 1; valid && k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            valid = (next & 0xc0) == 0x80;
            code = code << 6 | (next & 0x3f);
        }
        // Overlong forms, surrogates and values past U+10FFFF are invalid too.
        static constexpr uint32_t kMin[4] = {0, 0x80, 0x800, 0x10000};
        valid = valid && code >= kMin[extra] && code <= 0x10ffff && !(code >= 0xd800 && code < 0xe000);
        if (!valid) {
            out += static_cast<char16_t>(0xfffd);
            ++i;
            continue;
        }
        if (code >= 0x10000) {
            out += static_cast<char16_t>(0xd800 + ((code - 0x10000) >> 10));
            out += static_cast<char16_t>(0xdc00 + ((code - 0x10000) & 0x3ff));
        } else {
            out += static_cast<char16_t>(code);
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

} // namespace obl::code_page
