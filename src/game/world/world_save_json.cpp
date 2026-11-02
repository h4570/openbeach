#include "game/world/world_save_json.h"

#include "game/world/world_save.h"
#include "core/json_float.h"

#include <bit>
#include <cstring>
#include <set>
#include <utility>

namespace obl::world_save {
namespace {

using nlohmann::ordered_json;

struct Writer {
    const JsonContext& context;
    ordered_json& out;
    std::string& error;
    int32_t validCount = -1;

    template<class Value> void Put(const char* name, Value&& value)
    {
        if (const char* key = WorldJsonKey(name)) out[key] = std::forward<Value>(value);
    }

    void String(const char* name, const char (&text)[kMapIdSize]) { Put(name, context.narrowToUtf8({text, strnlen(text, kMapIdSize)})); }
    void Int(const char* name, int32_t value)
    {
        if (std::string_view(name) == "available_building_count") validCount = value;
        Put(name, value);
    }
    void UInt(const char* name, uint32_t value)
    {
        const char* key = WorldJsonKey(name);
        if (!key) return;
        if (!out.contains(key)) out[key] = ordered_json::array();
        out[key].push_back(value);
    }
    void Float(const char* name, const float& value) { Put(name, json_float::Bits(std::bit_cast<uint32_t>(value))); }
    void Bool(const char* name, uint8_t value)
    {
        if (value > 1 && error.empty()) error = std::string("World.") + name + ": bool value " + std::to_string(value);
        Put(name, value != 0);
    }
    void Ints(const char* name, const int* values, int count, Scope)
    {
        if (std::string_view(name) == "available_buildings") return AvailableBuildings(values, count);
        ordered_json list = ordered_json::array();
        for (int i = 0; i < count; ++i) list.push_back(values[i]);
        Put(name, std::move(list));
    }
    void Floats(const char* name, const float* values, int count, Scope)
    {
        ordered_json list = ordered_json::array();
        for (int i = 0; i < count; ++i) list.push_back(json_float::Bits(std::bit_cast<uint32_t>(values[i])));
        Put(name, std::move(list));
    }
    void ScriptState(const char* name, uint32_t state) { Put(name, static_cast<int32_t>(state)); }

    void AvailableBuildings(const int* ids, int count)
    {
        if (validCount < 0 || validCount > count) {
            if (error.empty()) error = "World.available_building_count: " + std::to_string(validCount) + " of " + std::to_string(count);
            return;
        }
        ordered_json list = ordered_json::array();
        std::set<std::string> seen;
        for (int i = 0; i < validCount; ++i) {
            const std::string key = context.buildingKey(ids[i]);
            if (key.empty() && error.empty())
                error = "World.available_buildings[" + std::to_string(i) + "]: no catalog building " + std::to_string(ids[i]);
            if (!key.empty() && !seen.insert(key).second && error.empty())
                error = "World.available_buildings: building " + key + " listed twice";
            list.push_back(key);
        }
        out["available_buildings"] = std::move(list);
    }
};

} // namespace

const char* WorldJsonKey(std::string_view name)
{
    // The boat list of raw pointers (rebuilt from the objects) are dropped; the valid-building pair is
    // written as `available_buildings` by the visitors themselves.
    for (std::string_view dropped : {"boat_list", "boat_count", "available_building_count", "available_buildings"})
        if (name == dropped) return nullptr;
    return name.data();
}

bool WriteWorldJson(const world::World& world, uint32_t scriptState, const JsonContext& context, ordered_json& out, std::string& error)
{
    out = ordered_json::object();
    error.clear();
    Writer writer{context, out, error};
    VisitFields(world, static_cast<const uint32_t&>(scriptState), writer);
    return error.empty();
}

} // namespace obl::world_save
