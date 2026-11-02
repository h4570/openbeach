#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// Small operating-system services of the game code in portable C++ (std::chrono, std::filesystem, getenv), so the simulation
// core reaches no Windows API (docs/architecture/headless/platform.md, "Platform calls"). Unit-tested on Windows and Linux.
namespace obl::os {

// Milliseconds of a monotonic clock as a wrapping 32-bit counter: the GetTickCount contract of the game clock (only differences
// of two readings mean anything; the origin is arbitrary).
uint32_t TickMs();

// An environment variable as UTF-8; nullopt when it is not set (an empty value is set). The C runtime's copy of the environment:
// variables the launcher sets before it loads openbeach.dll are seen, later changes made through the OS API are not.
std::optional<std::string> GetEnv(const char* name);
inline bool HasEnv(const char* name)
{
    return GetEnv(name).has_value();
}

struct DirectoryEntry {
    std::string name; // the platform's narrow file-name form (ANSI code page on Windows, bytes elsewhere): what fopen takes
    bool directory = false;
};

// The entries of `directory` (relative to the current directory; the game's `\` separators are accepted everywhere) without
// "." and "..", sorted by name ignoring ASCII case: the order NTFS lists ASCII names in, so callers whose later sort is not
// stable keep their tie order on every file system. Empty when the directory cannot be read; a name the narrow form cannot
// hold is left out.
std::vector<DirectoryEntry> ListDirectory(const std::string& directory);

// The name the platform opens for one of the game's paths, which mix `\` and `/` and spell names in any case
// (`savegames\save3.dat` for the directory `SaveGames`). Windows: unchanged. Elsewhere: `/` separators, and every
// component that does not exist as written is the entry of its directory with the same name ignoring ASCII case; from the
// first component nothing matches on, the rest stays as written (a file about to be created). Relative to the current
// directory, like the game's own paths.
std::string ResolvePath(const std::string& path);

// std::fopen of one of the game's paths through ResolvePath.
std::FILE* OpenFile(const std::string& path, const char* mode);

// Case-insensitive ASCII prefix/suffix tests for the game's `prefix*suffix` patterns.
bool MatchesPattern(const std::string& name, const char* prefix, const char* suffix);

// The current directory in the narrow file-name form (GetCurrentDirectoryA on Windows); "" when it cannot be had.
std::string CurrentDirectory();
#ifdef _WIN32
inline constexpr const char* kPathSeparator = "\\";
#else
inline constexpr const char* kPathSeparator = "/";
#endif

// Creates `path` when nothing exists there yet (a plain file of that name keeps it from being created, as the game expects).
void CreateDirectoryIfMissing(const char* path);

} // namespace obl::os
