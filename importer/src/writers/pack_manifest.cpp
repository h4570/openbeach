#include "importer/src/writers/pack_manifest.h"

#include "formats/schemas.h"
#include "core/sha256.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <span>
#include <vector>

namespace obl::convert {

namespace {

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// The obl.common path definition; checked here so the error names the file instead of a schema position.
bool ValidPackPath(const std::string& path)
{
    static const std::regex kPath("^[a-z0-9_.-]+(/[a-z0-9_.-]+)*$");
    if (!std::regex_match(path, kPath)) return false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        const auto segment = std::string_view(path).substr(start, end - start);
        if (segment == "." || segment == "..") return false;
        start = end + 1;
    }
    return true;
}

std::string HashOfText(std::string_view text)
{
    return sha256::Hex(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

} // namespace

std::string ImportReference(const std::map<std::string, std::string>& sourceHashes)
{
    // Only the hashes: the same copy read from a CD or its installation may spell its paths differently.
    std::vector<std::string> hashes;
    for (const auto& [path, hash] : sourceHashes) hashes.push_back(hash);
    std::sort(hashes.begin(), hashes.end());
    std::string text;
    for (const auto& hash : hashes) text += hash + '\n';
    return HashOfText(text);
}

int PackFileTier(std::string_view path)
{
    if (path.starts_with("maps/") && (EndsWith(path, "/terrain.png") || EndsWith(path, "/height.png"))) return 1;
    for (std::string_view media : {".png", ".wav", ".mp3", ".mpg", ".ogg"})
        if (EndsWith(path, media)) return 2;
    return 1;
}

bool BuildPackManifest(const PackSink& pack, const Source& source, std::optional<std::string> language, nlohmann::ordered_json& manifest,
        std::string& error, const std::function<void(int done, int total)>& report)
{
    std::vector<std::string> paths;
    for (std::string& path : pack.Files()) {
        if (path == "pack.json") continue; // its own hash is the pack's identity
        if (!ValidPackPath(path)) {
            error = path + ": not a valid pack path (lowercase [a-z0-9_.-], '/' separators)";
            return false;
        }
        paths.push_back(std::move(path));
    }
    // Byte order of the paths, whatever order the sink lists them in.
    std::sort(paths.begin(), paths.end());

    std::vector<std::string> languages;
    for (const auto& path : paths)
        if (path.starts_with("text/") && EndsWith(path, ".json") && path.find('/', 5) == std::string::npos)
            languages.push_back(path.substr(5, path.size() - 10));
    if (!language) {
        if (languages.empty()) {
            error = "the pack has no text/<lang>.json; convert the texts first";
            return false;
        }
        // Match startup's fallback order when the INI language has no imported table.
        language = languages.front();
        for (const char* candidate : {"en", "fr", "it", "de", "es", "pl"}) {
            if (std::find(languages.begin(), languages.end(), candidate) == languages.end()) continue;
            language = candidate;
            break;
        }
    } else if (std::find(languages.begin(), languages.end(), *language) == languages.end()) {
        error = "language " + *language + ": the pack has no text/" + *language + ".json";
        return false;
    }

    nlohmann::ordered_json files = nlohmann::ordered_json::object();
    int hashed = 0;
    for (const auto& path : paths) {
        if (report && hashed % 50 == 0) report(hashed, static_cast<int>(paths.size()));
        ++hashed;
        const auto bytes = pack.Read(path);
        if (!bytes) {
            error = path + ": cannot read";
            return false;
        }
        files[path] = {{"sha256", sha256::Hex(*bytes)}, {"tier", PackFileTier(path)}};
    }

    if (report) report(static_cast<int>(paths.size()), static_cast<int>(paths.size()));
    manifest = nlohmann::ordered_json::object();
    manifest["format"] = "obl.pack";
    manifest["version"] = kPackVersion;
    manifest["id"] = kConvertedPackId;
    manifest["title"] = kConvertedPackTitle;
    manifest["content_version"] = kConvertedContentVersion;
    manifest["language"] = *language;
    manifest["provenance"] = "converted";
    manifest["import"] = {{"reference", ImportReference(source.sha256)}};
    manifest["converter"] = kConverterName;
    manifest["files"] = std::move(files);
    return true;
}

bool WritePackManifest(PackSink& pack, const Source& source, std::optional<std::string> language, const json_schema::Registry& schemas,
        int& files, std::string& error, const std::function<void(int done, int total)>& report)
{
    nlohmann::ordered_json manifest;
    if (!BuildPackManifest(pack, source, std::move(language), manifest, error, report)) return false;
    if (!formats::ValidateDocument(schemas, "obl.pack", manifest, error)) {
        error = "pack.json: " + error;
        return false;
    }
    if (!pack.WriteText("pack.json", manifest.dump(2) + '\n', error)) return false;
    files = static_cast<int>(manifest["files"].size());
    return true;
}

} // namespace obl::convert
