#include "core/os.h"

#include "core/ascii_case.h"
#include "core/code_page.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <system_error>

namespace obl::os {

uint32_t TickMs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::optional<std::string> GetEnv(const char* name)
{
#ifdef _WIN32
    // The wide environment, so a path outside the ANSI code page arrives intact (as UTF-8).
    const std::u16string wideName = code_page::Utf8ToUtf16(name);
    wchar_t* value = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&value, &length, reinterpret_cast<const wchar_t*>(wideName.c_str())) != 0 || !value) return std::nullopt;
    std::string result = code_page::Utf16ToUtf8(reinterpret_cast<const char16_t*>(value));
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (!value) return std::nullopt;
    return std::string(value);
#endif
}

namespace {

std::filesystem::path GamePath(const std::string& path)
{
    return std::filesystem::path(ResolvePath(path));
}

} // namespace

std::string ResolvePath(const std::string& path)
{
#ifdef _WIN32
    return path;
#else
    std::string out;
    out.reserve(path.size());
    size_t at = 0;
    if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
        out = "/";
        at = 1;
    }
    bool resolving = true; // once a component matches nothing, the rest cannot exist either
    while (at <= path.size()) {
        size_t end = path.find_first_of("/\\", at);
        if (end == std::string::npos) end = path.size();
        std::string component = path.substr(at, end - at);
        if (!component.empty()) {
            if (resolving && component != "." && component != "..") {
                const std::string candidate = out + component;
                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec)) {
                    std::string match;
                    for (std::filesystem::directory_iterator it(out.empty() ? "." : out, ec), last; !ec && it != last; it.increment(ec)) {
                        const std::string name = it->path().filename().string();
                        // Several spellings of one name: the smallest bytes win, not the file system's listing order.
                        if (name.size() == component.size() && ascii::EqualsNoCase(name, component) && (match.empty() || name < match))
                            match = name;
                    }
                    if (match.empty())
                        resolving = false;
                    else
                        component = match;
                }
            }
            out += component;
            if (end < path.size()) out += '/';
        }
        at = end + 1;
    }
    return out;
#endif
}

std::FILE* OpenFile(const std::string& path, const char* mode)
{
    // A thin fopen wrapper: the caller owns and closes the handle, like with fopen itself.
    return std::fopen(ResolvePath(path).c_str(), mode); // NOLINT(cppcoreguidelines-owning-memory)
}

std::vector<DirectoryEntry> ListDirectory(const std::string& directory)
{
    std::vector<DirectoryEntry> entries;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(GamePath(directory.empty() ? "." : directory), ec), end; !ec && it != end;
            it.increment(ec)) {
        DirectoryEntry entry;
        try {
            entry.name = it->path().filename().string();
        } catch (const std::exception&) {
            continue; // not representable in the narrow form: the game could not open it either
        }
        if (entry.name == "." || entry.name == "..") continue;
        std::error_code typeError;
        entry.directory = it->is_directory(typeError);
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                [](char x, char y) { return static_cast<unsigned char>(ascii::Upper(x)) < static_cast<unsigned char>(ascii::Upper(y)); });
    });
    return entries;
}

bool MatchesPattern(const std::string& name, const char* prefix, const char* suffix)
{
    const size_t prefixLength = std::strlen(prefix), suffixLength = std::strlen(suffix);
    return name.size() >= prefixLength + suffixLength &&
            ascii::EqualsNoCase(std::string_view(name.data(), prefixLength), std::string_view(prefix, prefixLength)) &&
            ascii::EqualsNoCase(
                    std::string_view(name.data() + name.size() - suffixLength, suffixLength), std::string_view(suffix, suffixLength));
}

std::string CurrentDirectory()
{
    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    if (ec) return {};
    try {
        return current.string();
    } catch (const std::exception&) {
        return {};
    }
}

void CreateDirectoryIfMissing(const char* path)
{
    std::error_code ec;
    const std::filesystem::path target = GamePath(path);
    if (!std::filesystem::exists(target, ec) && !ec) std::filesystem::create_directory(target, ec);
}

} // namespace obl::os
