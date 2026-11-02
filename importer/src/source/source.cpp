#include "importer/src/source/source.h"

#include "importer/src/source/cabinet.h"
#include "openbeach/importer/byte_source.h"
#include "core/sha256.h"

#include <algorithm>
#include <cstring>
#include <system_error>
#include <vector>

namespace obl::convert {

namespace {

constexpr KnownExe kKnownExes[] = {
        {"983c098f69314c142ac8fd0d74fa34d098a49bd59f1aed88fb67a525c4b7f9e9", ExeBuild::DigitalOct2002},
        {"bdbc06034f298aff45acb49c1c085c1e10ff676fb7d5ad08b99ea33e01e2dd94", ExeBuild::CdAug2002}, // European CD
        {"2f56a5a52128909e211609cbdaadfb2fd67883869660663e909dbd430ff6a401", ExeBuild::CdAug2002}, // UK/US CD
};

struct KnownData {
    std::string_view stamp; // "YYYY:MM:DD hh:mm:ss", NUL-terminated in a 20-byte field
    DataRevision revision;
};

constexpr KnownData kKnownData[] = {
        {"2002:08:20 14:19:21", DataRevision::EuAug2002},
        {"2002:08:06 16:16:02", DataRevision::UkAug2002},
};

constexpr size_t kStampSize = 20;

} // namespace

std::string_view Name(ExeBuild build)
{
    return build == ExeBuild::DigitalOct2002 ? "digital-2002-10" : "cd-2002-08";
}

std::string_view Name(DataRevision revision)
{
    return revision == DataRevision::EuAug2002 ? "eu-2002-08" : "uk-2002-08";
}

std::span<const KnownExe> SupportedExes()
{
    return kKnownExes;
}

std::optional<ExeBuild> IdentifyExe(std::string_view sha256, std::span<const KnownExe> known)
{
    for (const KnownExe& exe : known)
        if (exe.sha256 == sha256) return exe.build;
    return std::nullopt;
}

std::optional<DataRevision> IdentifyData(std::span<const uint8_t> spriteHeader)
{
    if (spriteHeader.size() < kStampSize || spriteHeader[kStampSize - 1] != 0) return std::nullopt;
    const std::string_view stamp(reinterpret_cast<const char*>(spriteHeader.data()));
    for (const KnownData& known : kKnownData)
        if (known.stamp == stamp) return known.revision;
    return std::nullopt;
}

std::optional<std::filesystem::path> FindInstalledFile(const std::filesystem::path& root, std::string_view relative)
{
    const auto found = FindFile(*OpenDirectorySource(root), relative);
    if (!found) return std::nullopt;
    return found->empty() ? root : root / std::filesystem::path(std::u8string(found->begin(), found->end()));
}

bool IdentifySource(const ByteSource& files, Source& source, std::string& error, std::span<const KnownExe> known)
{
    source = {};
    error.clear();
    auto exePath = FindFile(files, kExePath);
    if (!exePath) {
        error = std::string(kExePath) + " not found under " + files.Describe("");
        return false;
    }
    const auto exeBytes = files.ReadAll(*exePath);
    if (!exeBytes) {
        error = "cannot read " + files.Describe(*exePath);
        return false;
    }
    const std::string exeHash = sha256::Hex(*exeBytes);
    auto exe = IdentifyExe(exeHash, known);
    if (!exe) {
        error = files.Describe(*exePath) + " is not a supported BeachLife.exe (sha256 " + exeHash + ")";
        return false;
    }

    // The sprite header of an installation, or on a CD root the same file from its cabinets (identical bytes).
    std::vector<uint8_t> header;
    std::string headerName;
    if (auto headerPath = FindFile(files, kSpriteHeaderPath)) {
        auto bytes = files.ReadAll(*headerPath);
        if (!bytes) {
            error = "cannot read " + files.Describe(*headerPath);
            return false;
        }
        header = std::move(*bytes);
        headerName = files.Describe(*headerPath);
    } else if (auto cabinetPath = FindFile(files, kCabinetHeaderPath)) {
        auto cabinet = Cabinet::Open(files, *cabinetPath, error);
        const CabinetFile* file = cabinet ? cabinet->Find(kSpriteHeaderPath, error) : nullptr;
        if (!file || !cabinet->ExtractToMemory(*file, header, error)) return false;
        headerName = files.Describe(*cabinetPath) + ": " + file->path;
    } else {
        error = std::string(kSpriteHeaderPath) + " (or a CD's " + std::string(kCabinetHeaderPath) + ") not found under " +
                files.Describe("");
        return false;
    }

    auto data = IdentifyData(header);
    if (!data) {
        error = headerName + " has an unrecognized build stamp";
        return false;
    }

    source.exe = *exe;
    source.data = *data;
    source.sha256[*exePath] = exeHash;
    source.sha256[std::string(kSpriteHeaderPath)] = sha256::Hex(header);
    return true;
}

} // namespace obl::convert
