#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Windows-1250 and Windows-1252 as Unicode code points: the converter reads the installed files' bytes through these, and the
// GUI loader writes the same bytes back for the legacy text parser (the engine's narrow game strings stay Windows-1252, code_page.h).
namespace obl::code_pages {

inline constexpr unsigned kUndefinedCodePoint = 0xFFFD; // the byte has no character in the code page

// Unicode code point of `byte` in Windows-1250 (`polish`) or Windows-1252. Bytes below 0x80 are ASCII; bytes the code page
// leaves undefined give kUndefinedCodePoint.
unsigned CodePoint(bool polish, unsigned byte);

// The byte that decodes to `unit`: the inverse of CodePoint, where an undefined byte decodes to its own C1 code point
// (U+0081 for 0x81), as the converter keeps it. False for a unit the code page does not have.
bool ByteOf(bool polish, char16_t unit, uint8_t& byte);

// Every unit of `text` as a byte of the code page; false at the first unit it does not have.
bool Encode(bool polish, std::u16string_view text, std::string& bytes);

} // namespace obl::code_pages
