#pragma once

#include "importer/src/source/pe_file.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace obl::convert::scan {

inline uint32_t LoadU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
            static_cast<uint32_t>(p[3]) << 24;
}

inline bool IsIdentifier(std::string_view s)
{
    if (s.size() < 2 || s.size() > 47) return false;
    return std::all_of(s.begin(), s.end(),
            [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// The identifier at `va`, if there is one.
inline std::optional<std::string> IdentifierAt(const PeFile& exe, uint32_t va)
{
    auto s = exe.CString(va, 48);
    if (!s || !IsIdentifier(*s)) return std::nullopt;
    return std::string(*s);
}

// The address of the C string `text` (whole string: NUL before and after), if it occurs exactly once in the file.
inline std::optional<uint32_t> UniqueCString(const PeFile& exe, std::string_view text)
{
    std::string pattern(1, '\0');
    pattern += text;
    pattern += '\0';
    auto bytes = exe.Bytes();
    auto first = std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end());
    if (first == bytes.end() || std::search(first + 1, bytes.end(), pattern.begin(), pattern.end()) != bytes.end()) return std::nullopt;
    return exe.VaOfFileOffset(static_cast<size_t>(first - bytes.begin()) + 1);
}

} // namespace obl::convert::scan
