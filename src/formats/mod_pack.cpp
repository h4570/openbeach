#include "formats/mod_pack.h"

#include "core/log.h"
#include "core/os.h"
#include "core/pack_path.h"
#include "formats/data_patch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>

namespace obl::mod_pack {

namespace {

std::vector<ActiveMod> g_active;
std::optional<std::vector<ActiveMod>> g_override; // test support: what saves and replays see instead of g_active

bool ReadString(const nlohmann::json& doc, const char* key, std::string& out, std::string& error)
{
    const auto it = doc.find(key);
    if (it == doc.end() || !it->is_string() || it->get<std::string>().empty()) {
        error = std::string("\"") + key + "\" is missing or not a string";
        return false;
    }
    out = it->get<std::string>();
    return true;
}

// The pack ids of `pack` (one) or `packs` (a list, for a mod that does not depend on content), never both.
bool ReadPacks(const nlohmann::json& doc, std::vector<std::string>& packs, std::string& error)
{
    const auto one = doc.find("pack");
    const auto many = doc.find("packs");
    if (one != doc.end() && many != doc.end()) {
        error = "names both \"pack\" and \"packs\"";
        return false;
    }
    if (one != doc.end()) {
        std::string pack;
        if (!ReadString(doc, "pack", pack, error)) return false;
        packs.push_back(pack);
    } else if (many != doc.end()) {
        if (!many->is_array() || many->empty()) {
            error = "packs is not a non-empty array";
            return false;
        }
        for (const auto& item : *many) {
            if (!item.is_string()) {
                error = "packs holds something that is not a pack id";
                return false;
            }
            packs.push_back(item.get<std::string>());
        }
    } else {
        error = "\"pack\" or \"packs\" is missing";
        return false;
    }
    for (const std::string& pack : packs)
        if (!pack_path::PackDirectory(pack)) {
            error = "pack \"" + pack + "\" is not a pack id";
            return false;
        }
    return true;
}

bool IsFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

// Whether `relative` (a generic path inside the mod) is a file a presentation mod may ship: the manifest, its scripts, text
// patches (the text loader lets them only add keys) and documents at the top. Anything else may reach the simulation: a sprite's
// frame count times an animation, a sound bank's size changes a random pick.
bool PresentationMayShip(const std::string& relative)
{
    if (relative == "mod.json") return true;
    if (relative.starts_with(std::string(kPresentationScripts) + "/")) return true;
    if (relative.starts_with("text/") && relative.find('/', 5) == std::string::npos && relative.ends_with(".patch.json")) return true;
    if (relative.find('/') == std::string::npos) {
        const std::string extension = std::filesystem::path(relative).extension().string();
        return extension.empty() || extension == ".md" || extension == ".txt";
    }
    return false;
}

// The first file of the mod a presentation mod may not ship, in name order; empty when there is none.
std::string FirstRefusedFile(const std::filesystem::path& directory)
{
    std::vector<std::string> refused;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) continue;
        const std::string relative = it->path().lexically_relative(directory).generic_string();
        if (!PresentationMayShip(relative)) refused.push_back(relative);
    }
    if (refused.empty()) return {};
    return *std::min_element(refused.begin(), refused.end());
}

bool HasPresentationScripts(const std::filesystem::path& directory)
{
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory / kPresentationScripts, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".lua" && IsFile(it->path())) return true;
    return false;
}

} // namespace

bool ParseManifest(std::string_view text, Manifest& manifest, std::string& error)
{
    nlohmann::json doc = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (!doc.is_object()) {
        error = "mod.json is not a JSON object";
        return false;
    }
    std::string format;
    if (!ReadString(doc, "format", format, error)) return false;
    if (format != kFormat) {
        error = "format \"" + format + "\" is not " + kFormat;
        return false;
    }
    if (!doc.contains("version") || !doc["version"].is_number_integer() || doc["version"].get<int>() != kVersion) {
        error = "version is not " + std::to_string(kVersion);
        return false;
    }
    Manifest m;
    if (!ReadString(doc, "id", m.id, error) || !ReadString(doc, "title", m.title, error) ||
            !ReadString(doc, "mod_version", m.modVersion, error) || !ReadPacks(doc, m.packs, error))
        return false;
    if (!pack_path::PackDirectory(m.id)) {
        error = "id \"" + m.id + "\" is not a mod id (lowercase letters, digits, _ and -)";
        return false;
    }
    if (const auto it = doc.find("scope"); it != doc.end()) {
        const std::string scope = it->is_string() ? it->get<std::string>() : std::string();
        if (scope == "presentation")
            m.scope = Scope::Presentation;
        else if (scope != "world") {
            error = "scope is not \"world\" or \"presentation\"";
            return false;
        }
    }
    if (const auto it = doc.find("api"); it != doc.end()) {
        if (!it->is_number_integer() || it->get<int64_t>() < 1 || it->get<int64_t>() > 1000000) {
            error = "api is not a positive integer";
            return false;
        }
        m.api = it->get<int>();
    }
    if (const auto it = doc.find("requires"); it != doc.end()) {
        if (!it->is_array()) {
            error = "requires is not an array";
            return false;
        }
        for (const auto& item : *it) {
            if (!item.is_string() || !pack_path::PackDirectory(item.get<std::string>())) {
                error = "requires holds something that is not a mod id";
                return false;
            }
            m.requiredMods.push_back(item.get<std::string>());
        }
    }
    manifest = std::move(m);
    return true;
}

bool CheckActive(const std::filesystem::path& gameDirectory, std::string_view packId, std::span<const std::string> active,
        std::vector<ActiveMod>& mods, std::string& error)
{
    mods.clear();
    for (const std::string& id : active) {
        const auto fail = [&](const std::string& reason) {
            error = "mod \"" + id + "\": " + reason;
            return false;
        };
        const auto directory = pack_path::ModDirectory(gameDirectory, id);
        if (!directory) return fail("not a mod id (lowercase letters, digits, _ and -)");
        if (std::any_of(mods.begin(), mods.end(), [&](const ActiveMod& m) { return m.manifest.id == id; })) return fail("listed twice");
        std::ifstream in(*directory / "mod.json", std::ios::binary);
        if (!in) return fail("no mod.json in " + directory->generic_string());
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ActiveMod mod;
        std::string reason;
        if (!ParseManifest(text, mod.manifest, reason)) return fail(reason);
        if (mod.manifest.id != id) return fail("mod.json names the id \"" + mod.manifest.id + "\"");
        const auto& packs = mod.manifest.packs;
        if (std::find(packs.begin(), packs.end(), packId) == packs.end()) {
            std::string names;
            for (const std::string& pack : packs) names += (names.empty() ? "\"" : ", \"") + pack + "\"";
            return fail("made for the pack " + names + ", not \"" + std::string(packId) + "\"");
        }
        if (mod.manifest.scope == Scope::Presentation)
            if (const std::string file = FirstRefusedFile(*directory); !file.empty())
                return fail("a presentation mod may not ship \"" + file +
                        "\" (only scripts/presentation/, text/<lang>.patch.json and documents at the top)");
        if (mod.manifest.api > kPresentationApi)
            return fail("needs the presentation script API " + std::to_string(mod.manifest.api) + ", this game has " +
                    std::to_string(kPresentationApi) + "; update the game");
        if (mod.manifest.api == 0 && HasPresentationScripts(*directory))
            return fail("ships presentation scripts but names no \"api\" in mod.json");
        for (const std::string& need : mod.manifest.requiredMods) {
            const bool earlier = std::any_of(mods.begin(), mods.end(), [&](const ActiveMod& m) { return m.manifest.id == need; });
            if (!earlier) {
                const bool later = std::find(active.begin(), active.end(), need) != active.end();
                return fail(later ? "requires \"" + need + "\", which must come earlier in the list"
                                  : "requires \"" + need + "\", which is not active");
            }
        }
        mod.directory = *directory;
        mod.hash = pack_path::ModContentHash(*directory);
        if (mod.hash.empty()) return fail("a file cannot be read");
        mods.push_back(std::move(mod));
    }
    return true;
}

const std::vector<ActiveMod>& Active()
{
    return g_override ? *g_override : g_active;
}

void OverrideActive(std::vector<ActiveMod> mods)
{
    g_override = std::move(mods);
}

void RestoreActive()
{
    g_override.reset();
}

bool Activate(const std::filesystem::path& gameDirectory, std::string_view packId, std::string_view configured, std::string& error)
{
    // OBL_MODS replaces the setting for tests and tools, even when it is empty.
    const auto env = os::GetEnv("OBL_MODS");
    const std::vector<std::string> ids = pack_path::ParseModList(env ? std::string_view(*env) : configured);
    std::vector<ActiveMod> mods;
    if (!CheckActive(gameDirectory, packId, ids, mods, error)) return false;
    std::vector<std::string> conflicts;
    const auto pack = pack_path::PackDirectory(packId);
    if (!pack) {
        error = "invalid pack id";
        return false;
    }
    if (!data_patch::Prepare(gameDirectory / *pack, ids, conflicts, error)) return false;
    for (const auto& conflict : conflicts) OBL_LOG("mod patch: %s", conflict.c_str());
    g_active = std::move(mods);
    pack_path::SetActiveMods(ids);
    return true;
}

} // namespace obl::mod_pack
