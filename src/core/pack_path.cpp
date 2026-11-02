#include "core/pack_path.h"

#include "core/os.h"

#include <set>

namespace obl::pack_path {

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

std::filesystem::path ModLayer(const std::filesystem::path& pack)
{
    // `packs/base` -> `mod`; a trailing separator (`packs/base/`) must not change the result.
    std::filesystem::path base = pack;
    if (!base.has_filename()) base = base.parent_path();
    return base.parent_path().parent_path() / "mod";
}

std::filesystem::path Resolve(const std::filesystem::path& pack, const std::filesystem::path& logical)
{
    std::error_code ec;
    const auto modFile = ModLayer(pack) / logical;
    if (std::filesystem::is_regular_file(modFile, ec)) return modFile;
    return pack / logical;
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
    for (const auto& layer : {ModLayer(pack) / directory, pack / directory}) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(layer, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (it->path().extension() == extension && it->is_regular_file(fileEc)) stems.insert(it->path().stem().string());
        }
    }
    return {stems.begin(), stems.end()};
}

} // namespace obl::pack_path
