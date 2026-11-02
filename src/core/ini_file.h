#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Portable ini files with the rules of the Windows profile API (GetPrivateProfileStringA, GetPrivateProfileIntA,
// WritePrivateProfileStringA), measured on Windows 11, so BeachLife.ini and openbeach.ini read and change as before
// (docs/architecture/headless/platform.md, "Platform calls"). The file is re-read on every call, like the API without its cache.
//
// Reading: lines end at \r\n, \n or \r. A section header is a line whose first non-blank character is `[`; its name runs to
// `]` or the end of the line and is compared trimmed and ignoring ASCII case. Only the FIRST section of a name is searched.
// An entry is a line with `=` that does not start with `;` (after blanks); its key is the trimmed text before the first `=`,
// compared ignoring case, the first match wins. The value is the trimmed rest, without one pair of matching outer quotes
// (" or '). Blanks are spaces and tabs.
// Writing an existing entry replaces the value text after `=` in place (the line keeps its key spelling, the blanks before the
// value are dropped, the ones after it stay, and the line ending stays). A new entry goes after the run of non-blank,
// non-comment lines that directly follows the section header; a new section goes at the end of the file. New lines end in
// \r\n; a last line without an ending gets one first.
namespace obl::ini {

// The value of `key`, or nullopt (missing file, section or key). `section` and `key` are trimmed before the comparison.
std::optional<std::string> Find(std::string_view text, std::string_view section, std::string_view key);

// The file text after writing `value` (raw, untrimmed) under `section`/`key`.
std::string Set(std::string_view text, std::string_view section, std::string_view key, std::string_view value);

// GetPrivateProfileStringA(section, key, fallback, buffer, size, path) as a string: the value, or `fallback` without its
// trailing blanks, cut to size - 1 characters (size 0 gives "").
std::string ReadString(
        const std::filesystem::path& path, std::string_view section, std::string_view key, std::string_view fallback, size_t size);

// GetPrivateProfileIntA: `fallback` for a missing or empty value; otherwise an optional sign and decimal digits, or 0x and hex
// digits, up to the first other character (none gives 0), wrapping at 32 bits.
int ReadInt(const std::filesystem::path& path, std::string_view section, std::string_view key, int fallback);

// WritePrivateProfileStringA(section, key, value, path): creates the file when missing. False when it cannot be written.
bool WriteString(const std::filesystem::path& path, std::string_view section, std::string_view key, std::string_view value);

} // namespace obl::ini
