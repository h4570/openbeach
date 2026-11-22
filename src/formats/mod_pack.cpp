#include "formats/mod_pack.h"

#include "core/os.h"
#include "core/pack_path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace obl::mod_pack {

namespace {

std::vector<ActiveMod> g_active;

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
            !ReadString(doc, "mod_version", m.modVersion, error) || !ReadString(doc, "pack", m.pack, error))
        return false;
    if (!pack_path::PackDirectory(m.id)) {
        error = "id \"" + m.id + "\" is not a mod id (lowercase letters, digits, _ and -)";
        return false;
    }
    if (!pack_path::PackDirectory(m.pack)) {
        error = "pack \"" + m.pack + "\" is not a pack id";
        return false;
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
        if (mod.manifest.pack != packId)
            return fail("made for the pack \"" + mod.manifest.pack + "\", not \"" + std::string(packId) + "\"");
        for (const std::string& need : mod.manifest.requiredMods) {
            const bool earlier = std::any_of(mods.begin(), mods.end(), [&](const ActiveMod& m) { return m.manifest.id == need; });
            if (!earlier) {
                const bool later = std::find(active.begin(), active.end(), need) != active.end();
                return fail(later ? "requires \"" + need + "\", which must come earlier in the list"
                                  : "requires \"" + need + "\", which is not active");
            }
        }
        mod.hash = pack_path::ModContentHash(*directory);
        if (mod.hash.empty()) return fail("a file cannot be read");
        mods.push_back(std::move(mod));
    }
    return true;
}

const std::vector<ActiveMod>& Active()
{
    return g_active;
}

bool Activate(const std::filesystem::path& gameDirectory, std::string_view packId, std::string_view configured, std::string& error)
{
    // OBL_MODS replaces the setting for tests and tools, even when it is empty.
    const auto env = os::GetEnv("OBL_MODS");
    const std::vector<std::string> ids = pack_path::ParseModList(env ? std::string_view(*env) : configured);
    std::vector<ActiveMod> mods;
    if (!CheckActive(gameDirectory, packId, ids, mods, error)) return false;
    g_active = std::move(mods);
    pack_path::SetActiveMods(ids);
    return true;
}

} // namespace obl::mod_pack
