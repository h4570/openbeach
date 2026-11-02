#include "importer/src/writers/script_convert.h"
#include "importer/src/readers/save/save_stream.h"
#include "core/ascii_case.h"
#include "importer/src/readers/script_vocabulary.h"
#include "importer/src/source/source.h"
#include "formats/script_pack.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

namespace obl::convert {
namespace {

// Scripts are a few hundred kilobytes; the cap only guards against a wrong file.
constexpr size_t kMaxScriptBytes = 4u << 20;

std::string Lower(std::string text)
{
    for (char& c : text) c = ascii::Lower(c);
    return text;
}

// The name after the last '/' of a source path.
std::string FileName(const std::string& path)
{
    return path.substr(path.rfind('/') + 1);
}

// The `.ble` files directly inside `dir`, sorted so the output does not depend on directory order.
bool ListScripts(const ByteSource& source, const std::string& dir, std::vector<std::string>& scripts, std::string& error)
{
    const auto entries = source.List(dir);
    if (!entries) {
        error = "cannot list " + source.Describe(dir);
        return false;
    }
    for (const SourceEntry& entry : *entries) {
        if (entry.directory) continue;
        const size_t dot = entry.name.rfind('.');
        if (dot != std::string::npos && dot != 0 && Lower(entry.name.substr(dot)) == ".ble") scripts.push_back(dir + "/" + entry.name);
    }
    std::sort(scripts.begin(), scripts.end());
    return true;
}

bool ReadScript(const ByteSource& source, const std::string& file, std::string& text, std::string& error)
{
    std::vector<uint8_t> bytes;
    if (!ReadBoundedFile(source, file, kMaxScriptBytes, bytes, error)) return false;
    if (!DecodeScript(bytes, text, error)) {
        error = FileName(file) + ": " + error;
        return false;
    }
    return true;
}

std::optional<PeFile> LoadExe(const ByteSource& source, std::string& error)
{
    const auto exePath = FindFile(source, kExePath);
    if (!exePath) {
        error = "BeachLife.exe not found in " + source.Describe("");
        return std::nullopt;
    }
    auto bytes = source.ReadAll(*exePath);
    if (!bytes) {
        error = "cannot read " + source.Describe(*exePath);
        return std::nullopt;
    }
    return PeFile::Parse(std::move(*bytes), error);
}

nlohmann::ordered_json Rows(const std::vector<ScriptName>& names)
{
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (const auto& name : names) rows.push_back({{"id", name.id}, {"name", name.name}});
    return rows;
}

// The JSON the loaders read (object: game/objects/script_program_ids.cpp, world: game/world/world_script_load_ids.cpp), indented by one space.
bool WriteVocabulary(PackSink& out, const std::string& path, nlohmann::ordered_json document, std::string& error)
{
    return out.WriteText(path, document.dump(1) + '\n', error);
}

// ---- feedback vocabulary -------------------------------------------------------------------------------------------------------------
// `scripts/feedback_vocabulary.json` (game/feedback/feedback_vocabulary.h): the notice keys and scenario words the scripts and the
// engine name, the notice shown for each missing staff class, and the hireable professions. The numbers are the engine's message types.

constexpr const char* kCommodityKeys[] = {"FEEDBACK_ACCOMMODATION", "FEEDBACK_ALCOHOL", "FEEDBACK_AMUSEMENT", "FEEDBACK_FIRSTAID",
        "FEEDBACK_HEADACHE", "FEEDBACK_MEALS", "FEEDBACK_PARTYFUN", "FEEDBACK_SHOWER", "FEEDBACK_SNACKS", "FEEDBACK_SOFTDRINKS",
        "FEEDBACK_SOUVENIRS", "FEEDBACK_SPORTSFUN", "FEEDBACK_SUNBATHING", "FEEDBACK_SWIMMINGFUN", "FEEDBACK_TOILET",
        "FEEDBACK_WATERSPORTSFUN", "FEEDBACK_STROLLAROUND", "FEEDBACK_LEAVERESORT", "FEEDBACK_VANDALISE", "FEEDBACK_FIGHT",
        "FEEDBACK_FLIRT", "FEEDBACK_UMBRELLAS", "FEEDBACK_SUNTANLOTION", "FEEDBACK_HARDSEX", "FEEDBACK_SLEEP", "FEEDBACK_SCARED"};

struct TypeWord {
    const char* word;
    int type;
};
constexpr TypeWord kTypeWords[] = {{"MESS_TYPE_SCENERY", 3}, {"MESS_TYPE_BUILDING", 4}, {"MESS_TYPE_GUEST", 5}, {"MESS_TYPE_VEHICLE", 6},
        {"MESS_TYPE_STAFF", 7}, {"MESS_TYPE_LIFEGUARD", 8}, {"MESS_TYPE_MECHANIC", 9}, {"MESS_TYPE_REP", 10}, {"MESS_TYPE_BUILDER", 11},
        {"MESS_TYPE_CLEANER", 12}, {"MESS_TYPE_SECURITYGUARD", 13}};

struct MissingNotice {
    const char* staffClass;
    const char* key;
    const char* typeWord;
};
constexpr MissingNotice kMissingNotices[] = {{"Cleaner", "FEEDBACK_NO_CLEANERS", "MESS_TYPE_CLEANER"},
        {"Rep", "FEEDBACK_NO_REPS", "MESS_TYPE_REP"}, {"Mechanic", "FEEDBACK_NO_MECHANICS", "MESS_TYPE_MECHANIC"},
        {"SecurityGuard", "FEEDBACK_NO_SECURITY", "MESS_TYPE_SECURITYGUARD"},
        {"Lifeguard", "FEEDBACK_NO_LIFEGUARDS", "MESS_TYPE_LIFEGUARD"}};

constexpr const char* kProfessionNames[] = {"Cleaner", "Builder", "Mechanic", "Rep", "Lifeguard", "SecurityGuard"}; // hire_type = the index

bool WriteScript(PackSink& out, const std::string& path, script_pack::Kind kind, const std::string& text, std::string& error)
{
    return out.WriteText(path, std::string(script_pack::Header(kind)) + text, error);
}

} // namespace

std::string FeedbackVocabularyJson()
{
    nlohmann::ordered_json document = {{"format", "obl.feedback_vocabulary"}, {"version", 1}};
    document["commodity_keys"] = nlohmann::ordered_json::array();
    for (const char* key : kCommodityKeys) document["commodity_keys"].push_back(key);
    document["message_types"] = nlohmann::ordered_json::array();
    document["message_types"].push_back({{"word", "MESS_TYPE_WORLD"}, {"silences", "world"}});
    document["message_types"].push_back({{"word", "MESS_TYPE_OBJECT"}, {"silences", "object"}});
    for (const TypeWord& entry : kTypeWords) document["message_types"].push_back({{"word", entry.word}, {"type", entry.type}});
    document["missing_staff"] = nlohmann::ordered_json::array();
    for (const MissingNotice& entry : kMissingNotices)
        document["missing_staff"].push_back({{"class", entry.staffClass}, {"key", entry.key}, {"type", entry.typeWord}});
    document["professions"] = nlohmann::ordered_json::array();
    int hireType = 0;
    for (const char* name : kProfessionNames) document["professions"].push_back({{"name", name}, {"hire_type", hireType++}});
    return document.dump(1) + '\n';
}

bool DecodeScript(std::span<const uint8_t> file, std::string& text, std::string& error)
{
    if (file.empty()) {
        error = "script is empty";
        return false;
    }
    text.clear();
    text.reserve(file.size());
    for (size_t i = 0; i < file.size(); ++i) {
        const auto byte = static_cast<unsigned char>(file[i] ^ 0xFFu);
        if (byte >= 0x80) {
            error = "byte " + std::to_string(i) + " is not ASCII";
            return false;
        }
        text.push_back(static_cast<char>(byte));
    }
    return true;
}

bool ConvertScripts(const ByteSource& source, PackSink& out, int& files, std::string& error)
{
    files = 0;
    try {
        const auto objectDir = FindFile(source, "Data/Script/Object");
        const auto scenarioDir = FindFile(source, "Data/Scenario");
        if (!objectDir || !scenarioDir) {
            error = "no Data/Script/Object or Data/Scenario in the installation";
            return false;
        }

        std::vector<std::string> objects;
        if (!ListScripts(source, *objectDir, objects, error)) return false;
        if (objects.empty()) {
            error = "no object scripts in " + source.Describe(*objectDir);
            return false;
        }
        for (const auto& file : objects) {
            std::string text;
            if (!ReadScript(source, file, text, error)) return false;
            const std::string fileName = FileName(file);
            const auto name = Lower(fileName.substr(0, fileName.rfind('.'))) + ".obs";
            if (!WriteScript(out, "scripts/object/" + name, script_pack::Kind::Object, text, error)) return false;
            ++files;
        }

        const auto scenarioEntries = source.List(*scenarioDir);
        if (!scenarioEntries) {
            error = "cannot list " + source.Describe(*scenarioDir);
            return false;
        }
        std::vector<std::string> folders;
        for (const SourceEntry& entry : *scenarioEntries)
            if (entry.directory) folders.push_back(*scenarioDir + "/" + entry.name);
        std::sort(folders.begin(), folders.end());
        for (const auto& folder : folders) {
            std::string id;
            const auto folderName = FileName(folder);
            if (!script_pack::ScenarioId(folderName, id)) {
                error = "scenario folder " + folderName + " has no id (expected Scenario<n> or SandScenario<n>)";
                return false;
            }
            std::vector<std::string> scripts;
            if (!ListScripts(source, folder, scripts, error)) return false;
            if (scripts.size() != 1) {
                error = folderName + ": expected one .ble script, found " + std::to_string(scripts.size());
                return false;
            }
            std::string text;
            if (!ReadScript(source, scripts.front(), text, error)) return false;
            if (!WriteScript(out, "scenarios/" + id + "/world.wsc", script_pack::Kind::World, text, error)) return false;
            ++files;
        }
        const auto exe = LoadExe(source, error);
        if (!exe) return false;
        ScriptTables tables;
        if (!ExtractScriptVocabulary(*exe, tables, error)) return false;
        nlohmann::ordered_json objectIds = {{"format", "obl.object_script_vocabulary"}, {"version", 1}};
        objectIds["states"] = Rows(tables.states);
        objectIds["messages"] = Rows(tables.messages);
        if (!WriteVocabulary(out, "scripts/object_vocabulary.json", objectIds, error)) return false;
        nlohmann::ordered_json worldIds = {{"format", "obl.world_script_vocabulary"}, {"version", 1}};
        worldIds["messages"] = Rows(tables.messages);
        if (!WriteVocabulary(out, "scripts/world_vocabulary.json", worldIds, error)) return false;
        if (!out.WriteText("scripts/feedback_vocabulary.json", FeedbackVocabularyJson(), error)) return false;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace obl::convert
