#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::mods {

inline constexpr size_t kMaxModZipBytes = 64 * 1024 * 1024;
inline constexpr size_t kMaxModFileBytes = 32 * 1024 * 1024;
inline constexpr size_t kMaxModExpandedBytes = 128 * 1024 * 1024;
inline constexpr size_t kMaxModZipEntries = 4096;

// A validated, single-mod archive. Inspection does not create any files.
class ModArchive {
public:
    bool Read(std::span<const uint8_t> zip, std::string_view packId, std::string& error);
    const std::string& Id() const { return m_id; }

    enum class Result { Installed, AlreadyInstalled, Failed };
    Result Write(const std::filesystem::path& gameDir, bool replace, std::string& error) const;

private:
    struct File {
        std::string name;
        std::string bytes;
    };

    std::string m_id;
    std::string m_packId;
    std::vector<File> m_files;
};

bool RemoveMod(const std::filesystem::path& gameDir, std::string_view id, std::string& error);

} // namespace obl::mods
