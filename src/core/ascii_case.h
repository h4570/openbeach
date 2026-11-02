#pragma once

#include "core/char16.h"
#include <cstddef>
#include <string_view>

namespace obl::ascii {

// Names are compared like `_stricmp` in the C locale, so only 'A'-'Z' fold to lower case and every other byte, including
// the bytes of a non-ASCII code page, compares as it is. Script and asset names are plain ASCII; a locale-aware fold would
// make the result depend on the host.
constexpr char Lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

constexpr char Upper(char c) noexcept
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
}

// Avoid the host wide CRT: obl-sim uses 16-bit wchar_t even when libc uses 32-bit units.
constexpr Char16 Lower(Char16 c) noexcept
{
    return c >= u'A' && c <= u'Z' ? static_cast<Char16>(c + (u'a' - u'A')) : c;
}

// Equal lengths and equal bytes after `Lower`. Taking views keeps one helper for `std::string` and for NUL-terminated names
// (callers pass `strlen`-bounded names): the length check replaces the loops that ran to the terminating NUL.
inline bool EqualsNoCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (Lower(a[i]) != Lower(b[i])) return false;
    return true;
}

// True when `needle` occurs in `text` without regard to case.
inline bool ContainsNoCase(std::string_view text, std::string_view needle) noexcept
{
    for (std::size_t i = 0; i + needle.size() <= text.size(); ++i)
        if (EqualsNoCase(text.substr(i, needle.size()), needle)) return true;
    return false;
}

// Byte order after `Upper`: names sort the same in every spelling, with '_' after the letters.
inline bool LessUpper(std::string_view a, std::string_view b) noexcept
{
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const auto x = static_cast<unsigned char>(Upper(a[i]));
        const auto y = static_cast<unsigned char>(Upper(b[i]));
        if (x != y) return x < y;
    }
    return a.size() < b.size();
}

inline bool EqualsNoCase(Text16View a, Text16View b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (Lower(a[i]) != Lower(b[i])) return false;
    return true;
}

} // namespace obl::ascii
