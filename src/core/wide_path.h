#pragma once

#include "core/char16.h"
#include "core/code_page.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

namespace obl {

// A file-system path from the game's 16-bit text (the game directory, OBL_* paths). std::filesystem converts wchar_t
// through the C++ library's wide codecvt, which on Linux is built for a 4-byte wchar_t and misreads the obl-sim's 16-bit
// units (docs/architecture/linux-host.md): there the text goes to the path as UTF-8, and the game's `\` separators become `/`.
inline std::filesystem::path WidePath(Text16View text)
{
#ifdef _WIN32
    return std::filesystem::path(text);
#else
    std::string utf8 = code_page::Utf16ToUtf8({reinterpret_cast<const char16_t*>(text.data()), text.size()});
    std::replace(utf8.begin(), utf8.end(), '\\', '/');
    return std::filesystem::path(utf8);
#endif
}

// A path from UTF-8 (os::GetEnv values); a plain std::string would be read in the ANSI code page on Windows.
inline std::filesystem::path Utf8Path(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// The game's 16-bit text as UTF-8, for logs and narrow APIs.
inline std::string WideUtf8(Text16View text)
{
    return code_page::Utf16ToUtf8({reinterpret_cast<const char16_t*>(text.data()), text.size()});
}

// A file-system path as UTF-8, for logs (path::string() is the ANSI code page on Windows).
inline std::string PathUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace obl
