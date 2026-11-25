#include "formats/mod_pack.h"

#include "formats/key_actions.h"
#include "core/log.h"
#include "core/os.h"
#include "core/pack_path.h"
#include "core/settings_schema.h"
#include "formats/data_patch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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

// One entry of `settings`: {"id", "type": "toggle|choice|number", "label", "hint", "default", and per type "options" or
// "min"/"max"/"step"}. The default is optional (off, the first option, the minimum) but must be a value of the field when given.
bool ReadSetting(const nlohmann::json& item, settings_schema::Field& field, std::string& error)
{
    using settings_schema::Kind;
    if (!item.is_object()) {
        error = "settings holds something that is not an object";
        return false;
    }
    const auto text = [&](const char* key, std::string& out, bool required) {
        const auto it = item.find(key);
        if (it == item.end()) {
            if (required) error = std::string("a setting needs \"") + key + "\"";
            return !required;
        }
        if (!it->is_string() || it->get<std::string>().size() > settings_schema::kMaxText || (required && it->get<std::string>().empty())) {
            error = std::string("setting \"") + field.id + "\": \"" + key + "\" is not a text of 1 to " +
                    std::to_string(settings_schema::kMaxText) + " characters";
            return false;
        }
        out = it->get<std::string>();
        return true;
    };
    std::string type;
    if (!text("id", field.id, true)) return false;
    if (!settings_schema::IsValidId(field.id)) {
        error = "setting id \"" + field.id + "\" is not an id (lowercase letters, digits, _ and -)";
        return false;
    }
    if (!text("type", type, true) || !text("label", field.label, true) || !text("hint", field.hint, false)) return false;
    const std::string where = "setting \"" + field.id + "\": ";
    std::optional<std::string> given;
    if (const auto it = item.find("default"); it != item.end()) {
        if (it->is_boolean())
            given = it->get<bool>() ? "1" : "0";
        else if (it->is_number())
            given = settings_schema::NumberText(it->get<double>());
        else if (it->is_string())
            given = it->get<std::string>();
        else {
            error = where + "\"default\" is not a boolean, number or text";
            return false;
        }
    }
    if (type == "toggle") {
        field.kind = Kind::Toggle;
        field.defaultValue = "0";
    } else if (type == "choice") {
        field.kind = Kind::Choice;
        const auto it = item.find("options");
        if (it == item.end() || !it->is_array() || it->empty() || it->size() > settings_schema::kMaxChoices) {
            error = where + "\"options\" is not a list of 1 to " + std::to_string(settings_schema::kMaxChoices) + " choices";
            return false;
        }
        for (const auto& option : *it) {
            settings_schema::Option entry;
            if (option.is_string()) {
                entry.value = entry.label = option.get<std::string>();
            } else if (option.is_object() && option.contains("value") && option["value"].is_string() && option.contains("label") &&
                    option["label"].is_string()) {
                entry.value = option["value"].get<std::string>();
                entry.label = option["label"].get<std::string>();
            } else {
                error = where + "an option is not a text or {\"value\", \"label\"}";
                return false;
            }
            // The value lives in an ini file: no line break, no ';' (a comment) and no blanks the reader would trim.
            const bool usable = !entry.value.empty() && entry.value.size() <= settings_schema::kMaxText && !entry.label.empty() &&
                    entry.label.size() <= settings_schema::kMaxText && entry.value.find_first_of("\r\n;") == std::string::npos &&
                    entry.value.front() != ' ' && entry.value.front() != '\t' && entry.value.back() != ' ' && entry.value.back() != '\t';
            if (!usable) {
                error = where + "an option's value is empty, too long, padded or holds a line break or ';' (an ini file holds it)";
                return false;
            }
            for (const auto& other : field.options)
                if (other.value == entry.value) {
                    error = where + "the option \"" + entry.value + "\" is listed twice";
                    return false;
                }
            field.options.push_back(std::move(entry));
        }
        field.defaultValue = field.options.front().value;
    } else if (type == "number") {
        field.kind = Kind::Number;
        const std::pair<const char*, double*> bounds[] = {{"min", &field.min}, {"max", &field.max}, {"step", &field.step}};
        for (const auto& [key, out] : bounds) {
            const auto it = item.find(key);
            if (it == item.end() || !it->is_number() || !std::isfinite(it->get<double>())) {
                error = where + "\"" + key + "\" is missing or not a number";
                return false;
            }
            *out = it->get<double>();
        }
        if (field.min >= field.max || field.step <= 0 || field.step > field.max - field.min) {
            error = where + "needs min < max and 0 < step <= max - min";
            return false;
        }
        field.defaultValue = settings_schema::NumberText(field.min);
    } else {
        error = where + "\"type\" is not \"toggle\", \"choice\" or \"number\"";
        return false;
    }
    if (given) {
        const auto canonical = settings_schema::Canonical(field, *given);
        if (!canonical) {
            error = where + "\"default\" is not a value of the setting";
            return false;
        }
        field.defaultValue = *canonical;
    }
    return true;
}

// A key list in mod.json: "F9", "P, F5" or ["P", "F5"].
bool ReadKeyList(const nlohmann::json& value, key_actions::KeyList& keys, std::string& error)
{
    std::string text;
    if (value.is_string()) {
        text = value.get<std::string>();
    } else if (value.is_array()) {
        for (const auto& item : value) {
            if (!item.is_string()) {
                error = "a key list holds something that is not a key name";
                return false;
            }
            text += (text.empty() ? "" : ",") + item.get<std::string>();
        }
    } else {
        error = "a key list is not a string or a list of strings";
        return false;
    }
    return key_actions::ParseKeys(text, keys, error);
}

bool ReadActions(const nlohmann::json& doc, std::vector<key_actions::ModAction>& actions, std::string& error)
{
    const auto it = doc.find("actions");
    if (it == doc.end()) return true;
    if (!it->is_array() || it->size() > kMaxActions) {
        error = "actions is not a list of at most " + std::to_string(kMaxActions) + " actions";
        return false;
    }
    for (const auto& item : *it) {
        key_actions::ModAction action;
        if (!item.is_object()) {
            error = "an action is not an object";
            return false;
        }
        if (!ReadString(item, "id", action.id, error)) return false;
        const std::string where = "action \"" + action.id + "\": ";
        if (action.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
            error = where + "the id may hold only lowercase letters, digits and _";
            return false;
        }
        for (const auto& other : actions)
            if (other.id == action.id) {
                error = where + "listed twice";
                return false;
            }
        if (const auto label = item.find("label"); label != item.end()) {
            if (!label->is_string()) {
                error = where + "\"label\" is not a string";
                return false;
            }
            action.label = label->get<std::string>();
        }
        if (const auto keys = item.find("keys"); keys != item.end()) {
            std::string reason;
            if (!ReadKeyList(*keys, action.keys, reason)) {
                error = where + reason;
                return false;
            }
        }
        actions.push_back(std::move(action));
    }
    return true;
}

bool ReadBindings(const nlohmann::json& doc, std::vector<key_actions::Binding>& bindings, std::string& error)
{
    const auto it = doc.find("bindings");
    if (it == doc.end()) return true;
    if (!it->is_object() || it->size() > kMaxActions) {
        error = "bindings is not an object of at most " + std::to_string(kMaxActions) + " entries";
        return false;
    }
    for (const auto& [name, value] : it->items()) {
        key_actions::Binding binding;
        binding.action = name;
        std::string reason;
        if (!ReadKeyList(value, binding.keys, reason)) {
            error = "binding \"" + name + "\": " + reason;
            return false;
        }
        bindings.push_back(std::move(binding));
    }
    return true;
}

bool ReadSettings(const nlohmann::json& doc, std::vector<settings_schema::Field>& settings, std::string& error)
{
    const auto it = doc.find("settings");
    if (it == doc.end()) return true;
    if (!it->is_array() || it->size() > kMaxSettings) {
        error = "settings is not a list of at most " + std::to_string(kMaxSettings) + " settings";
        return false;
    }
    for (const auto& item : *it) {
        settings_schema::Field field;
        if (!ReadSetting(item, field, error)) return false;
        for (const auto& other : settings)
            if (other.id == field.id) {
                error = "setting \"" + field.id + "\" is listed twice";
                return false;
            }
        settings.push_back(std::move(field));
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
bool PresentationMayShip(const std::filesystem::path& file, const std::string& relative)
{
    if (relative == "mod.json") return true;
    // A GUI layout patch changes only what the screen shows; a JSON data patch under gui/ is judged by its target below.
    if (relative.starts_with("gui/") && relative.find('/', 4) == std::string::npos && relative.ends_with(".patch.json")) {
        if (data_patch::PatchFormat(file) == data_patch::kGuiPatchFormat) return true;
        return !data_patch::IsSimulationDataPath(relative.substr(0, relative.size() - 11) + ".json");
    }
    if (relative.starts_with(std::string(kPresentationScripts) + "/")) return true;
    if (relative.starts_with("text/") && relative.find('/', 5) == std::string::npos && relative.ends_with(".patch.json")) return true;
    // Images for the scripts' overlay: only obl.overlay.image reads overlay/, and no pack has that directory, so nothing is shadowed.
    if (relative.starts_with(std::string(kOverlayImages) + "/") && relative.find('/', 8) == std::string::npos && relative.ends_with(".png"))
        return true;
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
        if (!PresentationMayShip(it->path(), relative)) refused.push_back(relative);
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

bool AreIncompatible(const Manifest& a, const Manifest& b)
{
    const auto names = [](const Manifest& m, const std::string& who) {
        return std::find(m.conflictingMods.begin(), m.conflictingMods.end(), who) != m.conflictingMods.end();
    };
    return names(a, b.id) || names(b, a.id);
}

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
    const auto readIds = [&](const char* key, std::vector<std::string>& out) {
        const auto it = doc.find(key);
        if (it == doc.end()) return true;
        if (!it->is_array()) {
            error = std::string(key) + " is not an array";
            return false;
        }
        for (const auto& item : *it) {
            if (!item.is_string() || !pack_path::PackDirectory(item.get<std::string>())) {
                error = std::string(key) + " holds something that is not a mod id";
                return false;
            }
            out.push_back(item.get<std::string>());
        }
        return true;
    };
    if (!readIds("requires", m.requiredMods) || !readIds("conflicts", m.conflictingMods)) return false;
    if (std::find(m.conflictingMods.begin(), m.conflictingMods.end(), m.id) != m.conflictingMods.end()) {
        error = "conflicts names the mod itself";
        return false;
    }
    for (const std::string& need : m.requiredMods)
        if (std::find(m.conflictingMods.begin(), m.conflictingMods.end(), need) != m.conflictingMods.end()) {
            error = "\"" + need + "\" is both in requires and in conflicts";
            return false;
        }
    if (!ReadSettings(doc, m.settings, error)) return false;
    if (!ReadActions(doc, m.actions, error) || !ReadBindings(doc, m.bindings, error)) return false;
    if (const auto it = doc.find("takes_over"); it != doc.end()) {
        if (!it->is_array()) {
            error = "takes_over is not an array";
            return false;
        }
        for (const auto& item : *it) {
            const std::string area = item.is_string() ? item.get<std::string>() : std::string();
            const bool action =
                    area.starts_with(kTakeOverActionPrefix) && key_actions::FindEngine(area.substr(kTakeOverActionPrefix.size()));
            if (!action && std::find(std::begin(kTakeOverAreas), std::end(kTakeOverAreas), area) == std::end(kTakeOverAreas)) {
                error = "takes_over names \"" + area + "\", which is not a default behavior a mod can take over";
                return false;
            }
            if (std::find(m.takesOver.begin(), m.takesOver.end(), area) == m.takesOver.end()) m.takesOver.push_back(area);
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
        // Nothing in a world mod can read a setting, and world scripts must never see one: the simulation takes no setting.
        if (mod.manifest.scope == Scope::World && !mod.manifest.settings.empty())
            return fail("a world mod may not declare \"settings\" (no setting reaches the simulation; make it a presentation mod)");
        if (mod.manifest.scope == Scope::Presentation)
            if (const std::string file = FirstRefusedFile(*directory); !file.empty())
                return fail("a presentation mod may not ship \"" + file +
                        "\" (only scripts/presentation/, overlay/<name>.png, text/<lang>.patch.json, gui/<name>.patch.json of presentation "
                        "data and documents at the top)");
        if (mod.manifest.api > kPresentationApi)
            return fail("needs the presentation script API " + std::to_string(mod.manifest.api) + ", this game has " +
                    std::to_string(kPresentationApi) + "; update the game");
        const bool scripts = HasPresentationScripts(*directory);
        if (mod.manifest.api == 0 && scripts) return fail("ships presentation scripts but names no \"api\" in mod.json");
        // Only a script can do what a taken-over default did.
        if (!mod.manifest.takesOver.empty() && !scripts)
            return fail("takes over \"" + mod.manifest.takesOver[0] + "\" but ships no scripts");
        // An action's press calls the mod's on_action.
        if (!mod.manifest.actions.empty() && !scripts) return fail("declares actions but ships no scripts");
        for (const std::string& need : mod.manifest.requiredMods) {
            const bool earlier = std::any_of(mods.begin(), mods.end(), [&](const ActiveMod& m) { return m.manifest.id == need; });
            if (!earlier) {
                const bool later = std::find(active.begin(), active.end(), need) != active.end();
                return fail(later ? "requires \"" + need + "\", which must come earlier in the list"
                                  : "requires \"" + need + "\", which is not active");
            }
        }
        for (const ActiveMod& other : mods) {
            if (AreIncompatible(mod.manifest, other.manifest))
                return fail("does not work with \"" + other.manifest.id + "\" (\"conflicts\" in mod.json); remove one of them");
        }
        mod.directory = *directory;
        mod.hash = pack_path::ModContentHash(*directory);
        if (mod.hash.empty()) return fail("a file cannot be read");
        mods.push_back(std::move(mod));
    }
    return true;
}

std::vector<std::string> TakeOverConflicts(std::span<const ActiveMod> mods)
{
    std::vector<std::string> lines;
    std::vector<std::string> names(std::begin(kTakeOverAreas), std::end(kTakeOverAreas));
    for (const auto& action : key_actions::EngineActions()) names.push_back(std::string(kTakeOverActionPrefix) + std::string(action.name));
    for (const std::string& area : names) {
        std::vector<std::string> takers;
        for (const ActiveMod& mod : mods)
            if (std::find(mod.manifest.takesOver.begin(), mod.manifest.takesOver.end(), area) != mod.manifest.takesOver.end())
                takers.push_back(mod.manifest.id);
        if (takers.size() < 2) continue;
        std::string list;
        for (const std::string& id : takers) list += (list.empty() ? "\"" : ", \"") + id + "\"";
        lines.push_back(
                "\"" + std::string(area) + "\" is taken over by " + list + "; \"" + takers.back() + "\" wins (later in load order)");
    }
    return lines;
}

std::vector<key_actions::ModKeys> ActionLayers(std::span<const ActiveMod> mods)
{
    std::vector<key_actions::ModKeys> layers;
    for (const ActiveMod& mod : mods) layers.push_back({mod.manifest.id, mod.manifest.actions, mod.manifest.bindings});
    return layers;
}

std::vector<std::string> KeyConflicts(std::span<const ActiveMod> mods, const key_actions::PlayerLookup& player)
{
    key_actions::Table table = key_actions::Resolve(ActionLayers(mods), player);
    table.conflicts.insert(table.conflicts.end(), table.problems.begin(), table.problems.end());
    return table.conflicts;
}

std::vector<InstalledMod> ListInstalled(const std::filesystem::path& gameDirectory)
{
    std::vector<InstalledMod> found;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(gameDirectory / "mods", ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        InstalledMod mod;
        mod.id = it->path().filename().string();
        if (!pack_path::PackDirectory(mod.id)) continue;
        mod.directory = it->path();
        std::ifstream in(mod.directory / "mod.json", std::ios::binary);
        if (!in) {
            mod.error = "no mod.json";
        } else {
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            mod.valid = ParseManifest(text, mod.manifest, mod.error);
            if (mod.valid && mod.manifest.id != mod.id) {
                mod.valid = false;
                mod.error = "mod.json names the id \"" + mod.manifest.id + "\"";
            }
        }
        found.push_back(std::move(mod));
    }
    std::sort(found.begin(), found.end(), [](const InstalledMod& a, const InstalledMod& b) { return a.id < b.id; });
    return found;
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
    return ActivateList(gameDirectory, packId, ids, error);
}

bool ActivateList(const std::filesystem::path& gameDirectory, std::string_view packId, std::span<const std::string> ids, std::string& error)
{
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
    for (const auto& conflict : TakeOverConflicts(mods)) OBL_LOG("mod takes_over: %s", conflict.c_str());
    g_active = std::move(mods);
    pack_path::SetActiveMods({ids.begin(), ids.end()});
    return true;
}

} // namespace obl::mod_pack
