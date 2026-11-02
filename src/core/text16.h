#pragma once

#include "core/char16.h"

#include <cstdarg>
#include <cstddef>
#include <string>

// The C library's wide-string functions for Char16 text. The C library's own take wchar_t, which is 4 bytes outside
// Windows; these work on 2-byte units on every host. Formatting and parsing keep the behavior the references were recorded
// with: the DLL's C runtime on Windows, its msvcrt-compatible reimplementation (core/wide_format.h) elsewhere.
namespace obl::text16 {

inline size_t Length(const Char16* text)
{
    return std::char_traits<Char16>::length(text);
}

// wcscpy / wcscat.
inline Char16* Copy(Char16* destination, const Char16* source)
{
    std::char_traits<Char16>::copy(destination, source, Length(source) + 1);
    return destination;
}

inline Char16* Append(Char16* destination, const Char16* source)
{
    Copy(destination + Length(destination), source);
    return destination;
}

// wcscmp: the order of the unsigned units.
inline int Compare(const Char16* a, const Char16* b)
{
    while (*a && *a == *b) ++a, ++b;
    return *a == *b ? 0 : (static_cast<unsigned>(*a) < static_cast<unsigned>(*b) ? -1 : 1);
}

// wcschr / wcsrchr.
inline const Char16* Find(const Char16* text, Char16 unit)
{
    for (;; ++text) {
        if (*text == unit) return text;
        if (!*text) return nullptr;
    }
}

inline Char16* FindLast(Char16* text, Char16 unit)
{
    Char16* found = nullptr;
    for (;; ++text) {
        if (*text == unit) found = text;
        if (!*text) return found;
    }
}

// The ISO swprintf/vswprintf: at most `capacity` units including the terminator; -1 when the text did not fit.
int FormatV(Char16* out, size_t capacity, const Char16* format, va_list args);
int Format(Char16* out, size_t capacity, const Char16* format, ...);

// wcstod / wcstol in the "C" locale; `end` (may be null) gets the first unit not used.
double ParseDouble(const Char16* text, Char16** end);
long ParseLong(const Char16* text, Char16** end, int base);

// std::to_wstring of an integer.
inline Text16 Decimal(long long value)
{
    const std::string digits = std::to_string(value);
    return Text16(digits.begin(), digits.end());
}

} // namespace obl::text16
