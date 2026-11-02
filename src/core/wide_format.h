#pragma once

#include <cstdarg>
#include <cstddef>

// printf-style formatting and number parsing of the game's 16-bit text with Microsoft's semantics, for hosts whose C
// library has no 16-bit wide functions (the Linux obl-sim, docs/architecture/linux-host.md). In a wide format `%s` and
// `%c` take wide text and `%S`/`%C` narrow (`%hs`/`%ls`/`%ws` force either); in a narrow format the other way round.
// Narrow text widens and narrows byte-transparently (code units 0-255), like msvcrt in the "C" locale.
namespace obl::wide_format {

enum class Flavor {
    Ucrt, // the C runtime (std::swprintf): two-digit exponents
    Msvcrt, // msvcrt.dll, the platform text convention: at least three exponent digits
};

// ISO form: writes at most `capacity` units including the terminator. Returns the units written without it, or -1 when
// the text did not fit (the buffer then holds the truncated text, terminated, when capacity > 0).
int Format(char16_t* out, size_t capacity, const char16_t* format, va_list args, Flavor flavor = Flavor::Ucrt);
// msvcrt's unbounded `swprintf(buffer, format, ...)` and `sprintf(buffer, format, ...)`.
int FormatUnbounded(char16_t* out, const char16_t* format, va_list args);
int FormatUnboundedNarrow(char* out, const char* format, va_list args);

// wcstod/wcstol in the "C" locale: leading white space, sign, decimal digits (no hex floats, inf or nan, as msvcrt).
// `end` gets the first unit not used, or `text` when nothing was converted.
double ParseDouble(const char16_t* text, const char16_t** end);
long ParseLong(const char16_t* text, const char16_t** end, int base);

} // namespace obl::wide_format
