#pragma once

// pack.json of a converted pack (docs/architecture/data-formats/converter/pack.md): identity, import reference, language and the
// hash and tier of every file the other commands wrote. Written last, after the pack's content is complete.

#include "importer/src/source/source.h"
#include "openbeach/importer/pack_sink.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace obl::json_schema {
class Registry;
}

namespace obl::convert {

inline constexpr std::string_view kConvertedPackId = "beachlife-2002";
inline constexpr std::string_view kConvertedPackTitle = "Beach Life (converted)";
inline constexpr std::string_view kConvertedContentVersion = "1.0.0";
inline constexpr std::string_view kConverterName = "obl-convert 1";
inline constexpr int kPackVersion = 2;

// The manifest's opaque import reference: a hash over the identifying files' hashes, so build names, source paths and
// the hashes themselves stay in the converter.
std::string ImportReference(const std::map<std::string, std::string>& sourceHashes);

// Tier of a pack file by kind (principles.md, "Data tiers"): media are 2, map terrain and height PNGs and every other
// file are 1.
int PackFileTier(std::string_view relativePath);

// The manifest of `pack` (every file except pack.json, sorted by path). `language` names the pack's default
// text table; without one use the first available in settings order (en, fr, it, de, es, pl),
// then the first other language by path. No tables is an error. A path that is
// not a valid pack path (lowercase, '/'-separated) is an error, not skipped.
bool BuildPackManifest(const PackSink& pack, const Source& source, std::optional<std::string> language, nlohmann::ordered_json& manifest,
        std::string& error, const std::function<void(int done, int total)>& report = {});

// Builds, validates against obl.pack and writes pack.json into `pack`.
bool WritePackManifest(PackSink& pack, const Source& source, std::optional<std::string> language, const json_schema::Registry& schemas,
        int& files, std::string& error, const std::function<void(int done, int total)>& report = {});

} // namespace obl::convert
