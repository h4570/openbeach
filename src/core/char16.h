#pragma once

#include <string>
#include <string_view>

// The game's text is UTF-16 in 2-byte units. wchar_t is 2 bytes only on Windows, so game text uses char16_t on every host
// (records keep their sizes and offsets, docs/architecture/linux-host.md); Windows API calls cast it to wchar_t at the call.
namespace obl {
using Char16 = char16_t;
using Text16 = std::basic_string<Char16>;
using Text16View = std::basic_string_view<Char16>;
} // namespace obl
