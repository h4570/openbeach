#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace obl::test {

// The local game copy: $OBL_OGDATA, og-data/ next to this checkout, or next to the main checkout of a worktree (its .git
// file names the main .git).
inline std::optional<std::filesystem::path> FindOgData()
{
    if (const char* env = std::getenv("OBL_OGDATA"); env && std::filesystem::is_directory(env)) return std::filesystem::path(env);
    const std::filesystem::path repo = OBL_REPO_DIR;
    if (std::filesystem::is_directory(repo / "og-data")) return repo / "og-data";
    std::ifstream git(repo / ".git");
    std::string line;
    if (git && std::getline(git, line) && line.rfind("gitdir: ", 0) == 0) {
        const std::string gitdir = line.substr(8);
        const size_t cut = gitdir.find("/.git/");
        if (cut != std::string::npos) {
            const std::filesystem::path main = std::filesystem::path(gitdir.substr(0, cut)) / "og-data";
            if (std::filesystem::is_directory(main)) return main;
        }
    }
    return std::nullopt;
}

// Locate European and UK/US CD fixtures by their directory names within the test data root.
inline constexpr const char* kCdCopies[] = {"iso", "Virtual-Resort-Spring-Break_Win_EN"};

} // namespace obl::test
