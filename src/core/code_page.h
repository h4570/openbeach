#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// The one code page of the game's narrow strings: Windows-1252 on every platform, so that a
// string does not depend on the PC's locale (CP_ACP is 1250 on a Polish Windows).
// Narrow game strings are byte strings: names from data files, scenario directories, text-table keys. Every byte widens to
// one UTF-16 unit and narrows back to the same byte (the five bytes 1252 leaves unassigned map to the C1 code points
// U+0081/8D/8F/90/9D, as Windows does), so a name read from the file system keeps its bytes through the game's wide text.
// Shipped data is ASCII, where all of this is the identity. Paths our own code builds from wide text are UTF-8 (Utf16ToUtf8).
namespace obl::code_page {

// One byte of Windows-1252 as UTF-16.
char16_t WidenByte(unsigned char byte);

// One UTF-16 unit as a Windows-1252 byte; '?' (and false in `mapped`) for a unit 1252 does not have.
char NarrowUnit(char16_t unit, bool* mapped = nullptr);

// MultiByteToWideChar(CP_1252, 0, text, -1, out, capacity): the NUL-terminated `text` with its NUL into `out`. Returns the
// units written including the NUL, or 0 when `capacity` is too small; unlike Windows the truncated result is still terminated
// (when `capacity` > 0).
size_t Widen(const char* text, char16_t* out, size_t capacity);

// WideCharToMultiByte(CP_1252, 0, text, -1, out, capacity, nullptr, nullptr), with the same result and truncation as Widen.
size_t Narrow(const char16_t* text, char* out, size_t capacity);

std::u16string Widen(std::string_view text);
std::string Narrow(std::u16string_view text);

// UTF-16 <-> UTF-8 (unpaired surrogates become U+FFFD; invalid UTF-8 bytes become U+FFFD). For paths and environment values,
// not for game text.
std::string Utf16ToUtf8(std::u16string_view text);
std::u16string Utf8ToUtf16(std::string_view text);

} // namespace obl::code_page
