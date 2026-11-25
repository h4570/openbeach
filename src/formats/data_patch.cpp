#include "formats/data_patch.h"

#include "core/json_schema.h"
#include "core/pack_path.h"
#include "formats/schemas.h"

#include <map>
#include <stdexcept>

namespace obl::data_patch {
namespace {
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;
using Files = std::map<fs::path, std::string>;
Files g_files;

Json Read(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + file.string());
    return Json::parse(stream);
}

// Who last wrote a JSON pointer of a document: the mod and the "mod, file, op, path" text errors and conflicts quote.
struct Origin {
    std::string mod, where;
    // An element a mod appended. A later mod editing it builds on that mod, it does not override a value.
    bool appended = false;
};
struct Document {
    Json value;
    std::string format;
    fs::path source;
    std::map<std::string, Origin> origins;
    std::string last;
    std::string logical; // the file's logical path, which a conflict names
};

std::string Escape(const std::string& key)
{
    return (Json::json_pointer() / key).to_string();
}

// Resolves a patch path to its node and the plain JSON pointer of that node. Arrays are entered only through a
// `[field=value]` selector, so a patch keeps naming the same element whatever the pack or an earlier mod appended.
// `set` may create the final object member.
Json& Target(Json& root, const std::string& path, std::string& pointer, bool set)
{
    if (path.empty() || path.front() != '/') throw std::runtime_error("expected /member path");
    Json* node = &root;
    size_t start = 1;
    while (true) {
        const size_t end = path.find('/', start);
        const auto segment = path.substr(start, end - start);
        const size_t bracket = segment.find('[');
        const auto key = Json::json_pointer("/" + segment.substr(0, bracket)).back();
        if (!node->is_object()) throw std::runtime_error("expected an object (array indices are unsupported)");
        node = set && end == std::string::npos && bracket == std::string::npos ? &(*node)[key] : &node->at(key);
        pointer += Escape(key);
        if (bracket != std::string::npos) {
            const size_t eq = segment.find('=', bracket);
            if (!segment.ends_with(']') || eq == std::string::npos || !node->is_array())
                throw std::runtime_error("expected an array selector [key=value]");
            const auto field = segment.substr(bracket + 1, eq - bracket - 1);
            const auto value = segment.substr(eq + 1, segment.size() - eq - 2);
            if (field != "key" && field != "name" && field != "id") throw std::runtime_error("unsupported selector field");
            Json* match = nullptr;
            size_t index = 0;
            for (size_t i = 0; i < node->size(); ++i) {
                auto& row = node->at(i);
                if (!row.contains(field)) continue;
                const auto primary = row.contains("key") ? "key" : row.contains("id") ? "id" : "name";
                if (field != primary) throw std::runtime_error("select this array by " + std::string(primary));
                const auto& id = row.at(field);
                if (id != value && !(id.is_number_integer() && id.dump() == value)) continue;
                if (match) throw std::runtime_error("ambiguous selector");
                match = &row;
                index = i;
            }
            if (!match) throw std::runtime_error("selector has no match");
            pointer += "/" + std::to_string(index);
            node = match;
        }
        if (end == std::string::npos) return *node;
        start = end + 1;
    }
}

// One pointer is the other or contains it.
bool Related(const std::string& a, const std::string& b)
{
    return a == b || a.starts_with(b + "/") || b.starts_with(a + "/");
}

void Assign(Document& doc, Json& target, const Json& value, const std::string& pointer, const Origin& write, bool merge,
        std::vector<Conflict>& conflicts)
{
    if (merge && value.is_object() && target.is_object()) {
        for (const auto& [key, member] : value.items()) Assign(doc, target[key], member, pointer + Escape(key), write, true, conflicts);
        return;
    }
    // Replacing an array (also through a parent object) could silently renumber entries held by the world.
    if (target.is_array()) throw std::runtime_error("use append or select an array element by key");
    if (target.is_object()) {
        for (const auto& [key, member] : target.items())
            if (!value.contains(key)) throw std::runtime_error("removing object members is unsupported");
        if (value.is_object()) {
            for (const auto& [key, member] : value.items())
                Assign(doc, target[key], member, pointer + Escape(key), write, false, conflicts);
            return;
        }
    }
    for (const auto& [old, previous] : doc.origins)
        if (Related(pointer, old) && previous.mod != write.mod && !previous.appended)
            conflicts.push_back({write.mod, previous.mod, doc.logical, pointer, write.where + " overrides " + previous.where});
    doc.origins[pointer] = write;
    target = value;
}

void Apply(Document& doc, const fs::path& file, const std::string& mod, std::vector<Conflict>& conflicts, std::string& where,
        std::vector<std::string>* errors)
{
    const Json patch = Read(file);
    if (patch.at("format") != "obl.json_patch" || !patch.at("version").is_number_integer() || patch.at("version") != 1 ||
            !patch.at("ops").is_array())
        throw std::runtime_error("expected obl.json_patch version 1 with an ops array");
    const auto base = where;
    for (size_t i = 0; i < patch.at("ops").size(); ++i) {
        where = base + " op " + std::to_string(i);
        auto apply = [&](Document& doc) {
            const auto& op = patch.at("ops").at(i);
            const auto path = op.at("path").get<std::string>();
            where += " " + path;
            std::string pointer;
            auto& target = Target(doc.value, path, pointer, op.at("op") == "set");
            if (op.at("op") == "append") {
                if (!target.is_array() || !op.at("items").is_array()) throw std::runtime_error("append expects arrays");
                for (const auto& item : op.at("items")) {
                    doc.origins[pointer + "/" + std::to_string(target.size())] = {mod, where, true};
                    target.push_back(item);
                }
            } else if (op.at("op") == "merge" || op.at("op") == "set") {
                const bool merge = op.at("op") == "merge";
                if (merge && (!target.is_object() || !op.at("value").is_object())) throw std::runtime_error("merge expects objects");
                Assign(doc, target, op.at("value"), pointer, {mod, where}, merge, conflicts);
            } else
                throw std::runtime_error("unknown operation");
            doc.last = where;
        };
        if (!errors) {
            apply(doc);
            continue;
        }
        auto candidate = doc;
        const auto conflictCount = conflicts.size();
        try {
            apply(candidate);
            doc = std::move(candidate);
        } catch (const std::exception& failure) {
            conflicts.resize(conflictCount);
            errors->push_back(where + ": " + failure.what());
        }
    }
}

} // namespace

namespace {

std::string FormatName(const Json& doc)
{
    const auto it = doc.is_object() ? doc.find("format") : doc.end();
    return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

std::string PatchFormat(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    return FormatName(Json::parse(stream, nullptr, false));
}

std::string PatchFormatText(std::string_view text)
{
    return FormatName(Json::parse(text.begin(), text.end(), nullptr, false));
}

bool IsSimulationDataPath(const fs::path& logical)
{
    const auto dir = logical.parent_path();
    return !(logical == "gui/object_browser.json" || logical == "gui/credits.json" || logical == "gui/layouts.json" || dir == "fonts" ||
            logical == "sprites/palette_modifiers.json");
}

Input::Input(const fs::path& file) : std::istream(nullptr)
{
    if (!g_files.empty()) {
        auto found = g_files.find(fs::absolute(file).lexically_normal());
        if (found != g_files.end()) {
            m_text.str(found->second);
            rdbuf(&m_text);
            return;
        }
    }
    if (m_file.open(file, std::ios::in | std::ios::binary)) rdbuf(&m_file);
}

namespace {

// Applies the patches of the active mods and checks the results before publishing them.
bool Build(const fs::path& pack, std::span<const std::string> mods, std::vector<Conflict>& conflicts, Files& files, std::string& error,
        std::vector<std::string>* errors = nullptr)
{
    std::map<fs::path, Document> documents;
    conflicts.clear();
    const size_t initialErrors = errors ? errors->size() : 0;
    const auto report = [&](std::string message) {
        error = std::move(message);
        if (errors) errors->push_back(error);
        return errors != nullptr;
    };
    const auto game = pack_path::GameDirectory(pack);
    for (const auto& mod : mods) {
        error = "mod " + mod;
        const auto directory = pack_path::ModDirectory(game, mod).value();
        std::map<fs::path, fs::path> patches;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (!entry.is_regular_file()) continue;
                auto relative = entry.path().lexically_relative(directory);
                auto name = relative.generic_string();
                if (!name.ends_with(".patch.json") || relative.parent_path() == "text" || relative.parent_path() == "recipes" ||
                        name == "catalog/index.patch.json" || name == "scripts/object_vocabulary.patch.json")
                    continue;
                // A GUI layout patch belongs to game/ui/gui_patch, which applies it when the layout loads.
                if (relative.parent_path() == "gui" && PatchFormat(entry.path()) == kGuiPatchFormat) continue;
                patches.emplace(name.substr(0, name.size() - 11) + ".json", entry.path());
            }
        } catch (const std::exception& failure) {
            if (!report(error + ": " + failure.what())) return false;
        }
        for (const auto& [logical, patch] : patches) {
            error = "mod " + mod + ": " + patch.string();
            try {
                if (logical == "pack.json" || logical == "mod.json") throw std::runtime_error("cannot patch a manifest");
                auto it = documents.find(logical);
                if (it == documents.end()) {
                    const auto source = pack_path::Resolve(pack, logical, mods);
                    auto value = Read(source);
                    const auto format = value.at("format").get<std::string>();
                    it = documents.emplace(logical, Document{std::move(value), format, source, {}, error, logical.generic_string()}).first;
                }
                Apply(it->second, patch, mod, conflicts, error, errors);
            } catch (const std::exception& failure) {
                if (!report(error + ": " + failure.what())) return false;
            }
        }
    }
    if (!documents.empty()) {
        json_schema::Registry schemas;
        if (!formats::LoadEmbeddedSchemas(schemas, error)) {
            report(error);
            return false;
        }
        for (const auto& [logical, doc] : documents) {
            const auto failures = schemas.Validate(doc.format, doc.value);
            for (const auto& failure : failures) {
                const auto pointer = failure.substr(0, failure.find(':'));
                // Blame the deepest write that touches the failing node, else the last operation on the file.
                std::string where = doc.last;
                size_t depth = 0;
                for (const auto& [path, write] : doc.origins)
                    if (Related(pointer, path) && path.size() >= depth) {
                        where = write.where;
                        depth = path.size();
                    }
                if (!report(where + ": " + failure)) return false;
            }
            if (failures.empty()) files.emplace(fs::absolute(doc.source).lexically_normal(), doc.value.dump());
        }
    }
    error.clear();
    return !errors || initialErrors == errors->size();
}

} // namespace

bool Prepare(const fs::path& pack, std::span<const std::string> mods, std::vector<std::string>& conflicts, std::string& error,
        std::vector<std::string>* errors)
{
    std::vector<Conflict> found;
    Files files;
    const bool valid = Build(pack, mods, found, files, error, errors);
    if (!valid && !errors) return false;
    conflicts.clear();
    for (const Conflict& conflict : found) conflicts.push_back(conflict.text);
    g_files.swap(files);
    return valid;
}

bool FindConflicts(const fs::path& pack, std::span<const std::string> mods, std::vector<Conflict>& conflicts, std::string& error)
{
    Files files;
    return Build(pack, mods, conflicts, files, error);
}

} // namespace obl::data_patch
