#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace obl::launcher {

struct OldSaveResult {
    std::vector<std::string> converted;
    std::vector<std::string> failed;
};

// Called before the game starts. Explicit paths keep development and installed launches on the same operation.
OldSaveResult ConvertOldSaves(const std::filesystem::path& saves, const std::filesystem::path& pack, const std::filesystem::path& converter,
        const std::function<void(const std::string&)>& log, std::chrono::milliseconds timeout = std::chrono::seconds(300));

} // namespace obl::launcher
