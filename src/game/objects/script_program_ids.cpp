#include "script_program_ids.h"

#include "game/objects/script_program_api.h"

#include <nlohmann/json.hpp>

namespace obl::object {

namespace {

constexpr char kScriptIdsFormat[] = "obl.object_script_vocabulary";
constexpr int kScriptIdsVersion = 1;

bool Fail(script_program::Vocabulary& out, std::string& error, const std::string& message)
{
    error = message;
    out = {};
    return false;
}

bool ReadTable(const nlohmann::json& doc, const char* key, size_t minimum, std::vector<script_program::NamedId>& table, std::string& error)
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

} // namespace

bool ParseScriptIds(std::string_view json, script_program::Vocabulary& out, std::string& error)
{
    out = {};
    nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) return Fail(out, error, "not valid JSON");
    if (doc.value("format", "") != kScriptIdsFormat || doc.value("version", 0) != kScriptIdsVersion)
        return Fail(out, error, "unsupported format/version");
    // The converter writes the shipped build's counts (135 states, 108 messages); an own pack lists the messages in full and
    // only the states its scripts use. Functions and values are the engine's own (script_program_api.h).
    if (!ReadTable(doc, "states", 1, out.states, error) || !ReadTable(doc, "messages", 100, out.messages, error))
        return Fail(out, error, error);
    out.functions = script_program::EngineFunctions();
    out.values = script_program::EngineValues();
    error.clear();
    return true;
}

} // namespace obl::object
