#pragma once

// Scratch paths for the unit tests. Parallel runs in other worktrees share the system temp directory, so a fixed name
// would let one run delete or lock another run's files.

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace obl::test {

// <temp>/<tag>-<random>-<clock><extension>. The name is not created; the caller creates what it needs.
inline std::filesystem::path UniqueTempPath(const std::string& tag, const std::string& extension = "")
{
    static std::random_device device;
    return std::filesystem::temp_directory_path() /
            (tag + "-" + std::to_string(device()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    extension);
}

// A UniqueTempPath that is removed with everything under it when the test leaves its scope. The guard also runs when a
// REQUIRE throws, so a failing test leaves nothing behind. Removal errors are ignored (a file a test still has mapped
// cannot be deleted on Windows and must not fail the test).
struct TempPath {
    std::filesystem::path path;

    explicit TempPath(const std::string& tag, const std::string& extension = "") : path(UniqueTempPath(tag, extension)) {}
    ~TempPath()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempPath(const TempPath&) = delete;
    TempPath& operator=(const TempPath&) = delete;
};

} // namespace obl::test
