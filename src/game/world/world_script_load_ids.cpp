#include "game/world/world_script_load.h"
#include "game/world/world_script_api.h"

#include <nlohmann/json.hpp>

namespace obl::world {
void AddCatalogModifiers(const tables::WorldParams& params, world_script_parse::Vocabulary& out)
{
    for (const auto& slot : params.m_desireSlots)
        if (!slot.m_scriptName.empty()) out.modifiers.push_back({slot.m_id, slot.m_scriptName});
}

bool ParseWorldScriptIds(std::string_view json, world_script_parse::Vocabulary& out, std::string& error)
{
    out = {};
    auto doc = nlohmann::json::parse(json, nullptr, false);
    auto fail = [&](const std::string& message) {
        out = {};
        error = message;
        return false;
    };
    if (doc.is_discarded() || !doc.is_object()) return fail("not valid JSON");
    if (!doc.contains("format") || doc["format"] != "obl.world_script_vocabulary" || !doc.contains("version") || doc["version"] != 1)
        return fail("unsupported format/version");
    auto read = [&](const char* key, size_t count, std::vector<world_script_parse::NamedId>& table) {
        if (!doc.contains(key) || !doc[key].is_array() || doc[key].size() != count) return false;
        for (const auto& row : doc[key]) {
            if (!row.is_object() || !row.contains("id") || !row["id"].is_number_integer() || !row.contains("name") ||
                    !row["name"].is_string() || row["name"].get<std::string>().empty())
                return false;
            table.push_back({row["id"].get<int>(), row["name"].get<std::string>()});
        }
        return true;
    };
    if (!read("messages", 108, out.messages)) return fail("incomplete or invalid vocabulary tables");
    // Functions and values are the engine's own (world_script_api.h).
    out.functions = world_script_parse::EngineFunctions();
    out.values = world_script_parse::EngineValues();
    error.clear();
    return true;
}
} // namespace obl::world
