#include "formats/object_vocabulary.h"

#include "core/pack_path.h"
#include "formats/data_patch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace obl::object_vocabulary {
namespace {

bool FailBase(std::vector<Entry>& states, std::vector<Entry>& messages, std::string& error, const std::string& reason)
{
    states.clear();
    messages.clear();
    error = reason;
    return false;
}

bool FailPatch(std::vector<std::string>& states, std::vector<std::string>& messages, std::string& error, const std::string& reason)
{
    states.clear();
    messages.clear();
    error = reason;
    return false;
}

bool ReadBaseTable(const nlohmann::json& doc, const char* key, size_t minimum, std::vector<Entry>& table, std::string& error)
{
    if (!doc.contains(key) || !doc[key].is_array() || doc[key].size() < minimum) {
        error = std::string(key) + ": expected at least " + std::to_string(minimum) + " entries";
        return false;
    }
    for (const auto& row : doc[key]) {
        if (!row.is_object() || !row.contains("id") || !row["id"].is_number_integer() || !row.contains("name") ||
                !row["name"].is_string() || row["name"].get<std::string>().empty()) {
            error = std::string(key) + ": expected {id, name} rows";
            return false;
        }
        table.push_back({row["id"].get<int>(), row["name"].get<std::string>()});
    }
    return true;
}

bool IsIdentifierChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool IsValidSymbol(std::string_view name, std::string_view prefix)
{
    if (name.size() <= prefix.size() || !name.starts_with(prefix)) return false;
    for (char c : name)
        if (!IsIdentifierChar(c)) return false;
    return true;
}

bool ReadPatchNames(const nlohmann::json& doc, const char* key, std::string_view prefix, std::vector<std::string>& out, std::string& error)
{
    const auto it = doc.find(key);
    if (it == doc.end()) return true;
    if (!it->is_array()) {
        error = std::string(key) + ": expected an array of names";
        return false;
    }
    std::set<std::string> seen;
    for (const auto& item : *it) {
        if (!item.is_string()) {
            error = std::string(key) + ": expected string names";
            return false;
        }
        const std::string name = item.get<std::string>();
        if (!IsValidSymbol(name, prefix)) {
            error = std::string(key) + ": \"" + name + "\" must start with " + std::string(prefix) +
                    " and use only letters, digits and '_'";
            return false;
        }
        if (!seen.insert(name).second) {
            error = std::string(key) + ": \"" + name + "\" is listed twice";
            return false;
        }
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return true;
}

int NextId(const std::vector<Entry>& table)
{
    int maxId = -1;
    for (const Entry& entry : table)
        if (entry.id > maxId) maxId = entry.id;
    return maxId + 1;
}

} // namespace

bool ParseBase(std::string_view json, std::vector<Entry>& states, std::vector<Entry>& messages, std::string& error)
{
    states.clear();
    messages.clear();
    nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) return FailBase(states, messages, error, "not valid JSON");
    if (doc.value("format", "") != kBaseFormat || doc.value("version", 0) != kBaseVersion)
        return FailBase(states, messages, error, "unsupported format/version");
    // The converter writes the shipped build's counts (135 states, 108 messages); an own pack lists the messages in full and
    // only the states its scripts use.
    if (!ReadBaseTable(doc, "states", 1, states, error) || !ReadBaseTable(doc, "messages", 100, messages, error))
        return FailBase(states, messages, error, error);
    error.clear();
    return true;
}

bool ParsePatch(std::string_view json, std::vector<std::string>& states, std::vector<std::string>& messages, std::string& error)
{
    states.clear();
    messages.clear();
    nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) return FailPatch(states, messages, error, "not valid JSON");
    if (doc.value("format", "") != kPatchFormat || doc.value("version", 0) != kPatchVersion)
        return FailPatch(states, messages, error, "unsupported format/version");
    for (const auto& [key, _] : doc.items())
        if (key != "format" && key != "version" && key != "states" && key != "messages")
            return FailPatch(states, messages, error, "unexpected key \"" + key + "\"");
    if (!ReadPatchNames(doc, "states", "STATE_", states, error) || !ReadPatchNames(doc, "messages", "MT_", messages, error))
        return FailPatch(states, messages, error, error);
    error.clear();
    return true;
}

bool LoadMerged(const std::filesystem::path& packDirectory, std::span<const std::string> mods, std::vector<Entry>& states,
        std::vector<Entry>& messages, std::string& error)
{
    states.clear();
    messages.clear();
    const std::filesystem::path basePath = pack_path::Resolve(packDirectory, kVocabularyPath, mods);
    data_patch::Input baseFile(basePath);
    if (!baseFile) {
        error = "file not found";
        return false;
    }
    const std::string baseJson{std::istreambuf_iterator<char>(baseFile), std::istreambuf_iterator<char>()};
    if (!ParseBase(baseJson, states, messages, error)) return false;
    if (mods.empty()) return true;

    std::map<std::string, std::string> stateOwner;
    std::map<std::string, std::string> messageOwner;
    for (const Entry& entry : states) stateOwner.emplace(entry.name, "the pack");
    for (const Entry& entry : messages) messageOwner.emplace(entry.name, "the pack");
    int nextStateId = NextId(states);
    // 108 (0x6c) is reserved as kNoMessage / kDroppedMessageType; mod message ids start at 109 or higher.
    constexpr int kFirstModMessageId = 109;
    int nextMessageId = std::max(NextId(messages), kFirstModMessageId);

    const std::filesystem::path gameDirectory = pack_path::GameDirectory(packDirectory);
    for (const std::string& mod : mods) {
        const auto modDirectory = pack_path::ModDirectory(gameDirectory, mod);
        if (!modDirectory) continue;
        const std::filesystem::path patchFile = *modDirectory / kPatchPath;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(patchFile, ec)) continue;
        std::ifstream in(patchFile, std::ios::binary);
        if (!in) {
            return FailBase(states, messages, error, "mod \"" + mod + "\" (" + std::string(kPatchPath) + "): cannot open file");
        }
        const std::string patchJson{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        std::vector<std::string> modStates;
        std::vector<std::string> modMessages;
        std::string patchError;
        if (!ParsePatch(patchJson, modStates, modMessages, patchError)) {
            return FailBase(states, messages, error, "mod \"" + mod + "\" (" + std::string(kPatchPath) + "): " + patchError);
        }
        const std::string currentOwner = "mod \"" + mod + "\"";
        for (const std::string& name : modStates) {
            if (const auto it = stateOwner.find(name); it != stateOwner.end()) {
                return FailBase(states, messages, error,
                        currentOwner + " (" + std::string(kPatchPath) + "): state \"" + name + "\" collides with " + it->second);
            }
            stateOwner.emplace(name, currentOwner);
            states.push_back({nextStateId++, name});
        }
        for (const std::string& name : modMessages) {
            if (const auto it = messageOwner.find(name); it != messageOwner.end()) {
                return FailBase(states, messages, error,
                        currentOwner + " (" + std::string(kPatchPath) + "): message \"" + name + "\" collides with " + it->second);
            }
            messageOwner.emplace(name, currentOwner);
            messages.push_back({nextMessageId++, name});
        }
    }
    error.clear();
    return true;
}

} // namespace obl::object_vocabulary
