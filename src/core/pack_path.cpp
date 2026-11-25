#include "core/pack_path.h"

#include "core/os.h"
#include "core/sha256.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>

namespace obl::pack_path {

namespace {

std::vector<std::string> g_activeMods;
std::map<std::string, std::filesystem::path, std::less<>> g_modDirectories;

} // namespace

std::optional<std::filesystem::path> PackDirectory(std::string_view id)
{
    if (id.empty()) return std::nullopt;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return std::nullopt;
    }
    return std::filesystem::path("packs") / std::string(id);
}

std::string SelectedId()
{
    if (auto id = os::GetEnv("OBL_PACK"); id && !id->empty()) return *id;
    return kDefaultPack;
}

std::filesystem::path Selected()
{
    return PackDirectory(SelectedId()).value_or(std::filesystem::path());
}

bool CheckSelected(const std::filesystem::path& gameDirectory, std::string& error)
{
    const std::string id = SelectedId();
    const auto directory = PackDirectory(id);
    if (!directory) {
        error = "OBL_PACK \"" + id + "\" is not a pack id (lowercase letters, digits, _ and -)";
        return false;
    }
    // The default pack is checked by each loader as it reads (with the setup hint); a pack named on purpose must exist, so a
    // misspelled id stops here instead of at the first loader.
    if (os::GetEnv("OBL_PACK").value_or("").empty()) return true;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(gameDirectory / *directory / "pack.json", ec)) {
        error = "no pack.json in " + (gameDirectory / *directory).generic_string();
        return false;
    }
    return true;
}

std::filesystem::path GameDirectory(const std::filesystem::path& pack)
{
    // `packs/base` -> the directory above `packs`; a trailing separator (`packs/base/`) must not change the result.
    std::filesystem::path base = pack;
    if (!base.has_filename()) base = base.parent_path();
    return base.parent_path().parent_path();
}

std::filesystem::path ModLayer(const std::filesystem::path& pack)
{
    return GameDirectory(pack) / "mod";
}

std::optional<std::filesystem::path> ModDirectory(const std::filesystem::path& gameDirectory, std::string_view id)
{
    if (const auto it = g_modDirectories.find(id); it != g_modDirectories.end()) return it->second;
    // The same character rules as a pack id, so an id can only name a directory under `mods/`.
    const auto directory = PackDirectory(id);
    if (!directory) return std::nullopt;
    return gameDirectory / "mods" / std::string(id);
}

std::optional<std::filesystem::path> ModCacheDirectory(const std::filesystem::path& gameDirectory, std::string_view id)
{
    const auto directory = PackDirectory(id);
    if (!directory) return std::nullopt;
    return gameDirectory / "mod-cache" / std::string(id);
}

void SetModDirectoryOverride(std::string id, std::filesystem::path directory)
{
    if (directory.empty())
        g_modDirectories.erase(id);
    else
        g_modDirectories.insert_or_assign(std::move(id), std::move(directory));
}

std::vector<std::string> ParseModList(std::string_view list)
{
    std::vector<std::string> ids;
    while (!list.empty()) {
        const size_t comma = list.find(',');
        std::string_view item = list.substr(0, comma);
        list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);
        if (!item.empty()) ids.emplace_back(item);
    }
    return ids;
}

void SetActiveMods(std::vector<std::string> ids)
{
    g_activeMods = std::move(ids);
}

const std::vector<std::string>& ActiveMods()
{
    return g_activeMods;
}

static std::vector<std::filesystem::path> Layers(const std::filesystem::path& pack, std::span<const std::string> mods)
{
    std::vector<std::filesystem::path> layers;
    const auto game = GameDirectory(pack);
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        // An id that is not valid is refused by the start-up check; here it simply names no layer.
        if (const auto directory = ModDirectory(game, *it)) layers.push_back(*directory);
        if (const auto cache = ModCacheDirectory(game, *it)) layers.push_back(*cache);
    }
    layers.push_back(game / "mod");
    layers.push_back(pack);
    return layers;
}

std::vector<std::filesystem::path> Layers(const std::filesystem::path& pack)
{
    return Layers(pack, g_activeMods);
}

// Line ends of a text file depend on the checkout (CRLF on Windows, LF elsewhere) and the engine reads both, so the hash
// sees CRLF as LF there; a file without CRLF keeps the hash of its bytes, and binary files are hashed as they are.
static std::optional<std::string> FileHash(const std::filesystem::path& file)
{
    static constexpr std::string_view kTextExtensions[] = {".json", ".lua", ".wsc", ".obs", ".md", ".txt"};
    const std::string extension = file.extension().string();
    bool isText = false;
    for (const auto text : kTextExtensions) isText = isText || extension == text;
    if (!isText) return sha256::OfFile(file);
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string normalized;
    normalized.reserve(content.size());
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r' && i + 1 < content.size() && content[i + 1] == '\n') continue;
        normalized.push_back(content[i]);
    }
    sha256::Hasher hasher;
    hasher.Update({reinterpret_cast<const uint8_t*>(normalized.data()), normalized.size()});
    return hasher.HexDigest();
}

std::string ModContentHash(const std::filesystem::path& modDirectory)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(modDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) files.push_back(it->path().lexically_relative(modDirectory));
    }
    if (ec) return {};
    std::vector<std::string> names;
    for (const auto& file : files) names.push_back(file.generic_string());
    std::sort(names.begin(), names.end());
    sha256::Hasher hasher;
    for (const auto& name : names) {
        const auto fileHash = FileHash(modDirectory / name);
        if (!fileHash) return {};
        const std::string line = name + '\n' + *fileHash + '\n';
        hasher.Update({reinterpret_cast<const uint8_t*>(line.data()), line.size()});
    }
    return hasher.HexDigest();
}

std::filesystem::path Resolve(const std::filesystem::path& pack, const std::filesystem::path& logical, std::span<const std::string> mods)
{
    std::error_code ec;
    for (const auto& layer : Layers(pack, mods)) {
        const auto file = layer / logical;
        if (std::filesystem::is_regular_file(file, ec)) return file;
    }
    // No layer has it: the pack's path, so the loader names the converted file in its error.
    return pack / logical;
}

std::filesystem::path Resolve(const std::filesystem::path& pack, const std::filesystem::path& logical)
{
    return Resolve(pack, logical, g_activeMods);
}

std::filesystem::path ResolveIn(const std::filesystem::path& directory, const std::filesystem::path& file)
{
    std::filesystem::path dir = directory;
    if (!dir.has_filename()) dir = dir.parent_path();
    return Resolve(dir.parent_path(), dir.filename() / file);
}

std::vector<std::string> ListStems(const std::filesystem::path& pack, const std::filesystem::path& directory, std::string_view extension)
{
    // A std::set keeps the names sorted and drops the duplicates a mod file shares with the pack.
    std::set<std::string> stems;
    for (const auto& base : Layers(pack)) {
        const auto layer = base / directory;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(layer, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (!(extension == ".json" && it->path().filename().string().ends_with(".patch.json")) && it->path().extension() == extension &&
                    it->is_regular_file(fileEc))
                stems.insert(it->path().stem().string());
        }
    }
    return {stems.begin(), stems.end()};
}

} // namespace obl::pack_path
