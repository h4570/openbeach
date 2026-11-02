#pragma once

#include "openbeach/importer/byte_source.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace obl::convert {

enum class ExeBuild { DigitalOct2002, CdAug2002 };
enum class DataRevision { EuAug2002, UkAug2002 };

std::string_view Name(ExeBuild build);
std::string_view Name(DataRevision revision);

struct KnownExe {
    std::string_view sha256; // lowercase hex
    ExeBuild build;
};

std::span<const KnownExe> SupportedExes();

std::optional<ExeBuild> IdentifyExe(std::string_view sha256, std::span<const KnownExe> known = SupportedExes());
// By the stamp at the start of Data/Sprites/BLSprite.hdr (the first bytes of the file are enough).
std::optional<DataRevision> IdentifyData(std::span<const uint8_t> spriteHeader);

// Relative paths of the files that identify a source, as the installation spells them.
inline constexpr std::string_view kExePath = "BeachLife.exe";
inline constexpr std::string_view kSpriteHeaderPath = "Data/Sprites/BLSprite.hdr";
// On a CD root the data is still in InstallShield cabinets behind this header (cabinet.h).
inline constexpr std::string_view kCabinetHeaderPath = "data1.hdr";

// `relative` (with '/') under the local directory `root`, matching every component without regard to ASCII case
// (FindFile); nullopt when a component is missing. For the CLI and tests; the converters read through a ByteSource.
std::optional<std::filesystem::path> FindInstalledFile(const std::filesystem::path& root, std::string_view relative);

struct Source {
    ExeBuild exe = ExeBuild::DigitalOct2002;
    DataRevision data = DataRevision::EuAug2002;
    std::map<std::string, std::string> sha256; // relative path -> hash of the identifying files
};

// Identifies the installation, or the CD root, in `files`; false with `error` naming the missing or unrecognized file. The
// hashes are keyed by installation path, so a CD and its installation give the same map. `known` as for IdentifyExe.
bool IdentifySource(const ByteSource& files, Source& source, std::string& error, std::span<const KnownExe> known = SupportedExes());

} // namespace obl::convert
