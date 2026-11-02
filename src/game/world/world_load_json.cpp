#include "game/world/world_load_json.h"

#include "game/world/world_save.h"
#include "game/world/world_save_json.h"
#include "game/world/save_json_values.h"

#include <bit>
#include <cstring>
#include <map>

namespace obl::world_save {
namespace {

using nlohmann::json;

// The World field table (world_save.h VisitFields) read from the document. A missing key gives the bridge's default; a dropped record
// (the `sizeof` constant, the boat list) reads as the zeros the bridge encodes for it.
struct Reader {
    const json& in;
    const ReadContext& context;
    std::string& error;
    std::map<std::string, uint32_t> seen; // occurrences of a repeated name (the objective flags)

    void Fail(const char* name, const std::string& why)
    {
        if (error.empty()) error = std::string("World.") + name + ": " + why;
    }
    // The value of `name`, or null when the key is missing or the record is dropped.
    const json* Value(const char* name)
    {
        const char* key = WorldJsonKey(name);
        if (!key) return nullptr;
        const auto it = in.find(key);
        return it == in.end() ? nullptr : &*it;
    }
    // An integer or float record's value by the rules of save_json_values.h.
    bool Integer(const char* name, const json& v, uint32_t& out)
    {
        const auto bits = IntegerBits(v);
        if (!bits) Fail(name, "not an integer");
        out = bits.value_or(0);
        return bits.has_value();
    }
    bool Float32(const char* name, const json& v, uint32_t& out)
    {
        const auto bits = FloatBits(v);
        if (!bits) Fail(name, "not a float");
        out = bits.value_or(0);
        return bits.has_value();
    }

    void String(const char* name, char (&text)[kMapIdSize])
    {
        std::string narrow;
        if (const json* v = Value(name)) {
            if (!v->is_string() || !context.utf8ToNarrow(v->get_ref<const std::string&>(), narrow)) {
                Fail(name, "text outside the pack's code page");
                return;
            }
        }
        if (narrow.size() >= kMapIdSize) {
            Fail(name, "text of " + std::to_string(narrow.size()) + " bytes");
            return;
        }
        // ReadString: the bytes and a terminator; the rest of the buffer keeps what it held.
        std::memcpy(text, narrow.data(), narrow.size());
        text[narrow.size()] = 0;
    }
    void Int(const char* name, int32_t& field)
    {
        if (std::string_view(name) == "available_building_count") {
            const auto it = in.find("available_buildings");
            field = it == in.end() ? 0 : static_cast<int32_t>(it->size());
            return;
        }
        uint32_t value = 0;
        if (const json* v = Value(name)) Integer(name, *v, value);
        field = static_cast<int32_t>(value);
    }
    void UInt(const char* name, uint32_t& field)
    {
        const uint32_t i = seen[name]++;
        uint32_t value = 0;
        if (const json* list = Value(name)) {
            if (!list->is_array() || i >= list->size())
                Fail(name, "no element " + std::to_string(i));
            else
                Integer(name, (*list)[i], value);
        }
        field = value;
    }
    void Float(const char* name, float& field)
    {
        uint32_t bits = 0;
        if (const json* v = Value(name)) Float32(name, *v, bits);
        field = std::bit_cast<float>(bits);
    }
    void Bool(const char* name, uint8_t& field)
    {
        uint32_t value = 0;
        if (const json* v = Value(name)) Integer(name, *v, value);
        field = static_cast<uint8_t>(value);
    }
    void Ints(const char* name, int* values, int count, Scope)
    {
        if (std::string_view(name) == "available_buildings") return AvailableBuildings(values, count);
        const json* list = Value(name);
        if (list && (!list->is_array() || list->size() != static_cast<size_t>(count)))
            return Fail(name, "not a list of " + std::to_string(count));
        for (int i = 0; i < count; ++i) {
            uint32_t value = 0;
            if (list) Integer(name, (*list)[static_cast<size_t>(i)], value);
            values[i] = static_cast<int>(value);
        }
    }
    void Floats(const char* name, float* values, int count, Scope)
    {
        const json* list = Value(name);
        if (list && (!list->is_array() || list->size() != static_cast<size_t>(count)))
            return Fail(name, "not a list of " + std::to_string(count));
        for (int i = 0; i < count; ++i) {
            uint32_t bits = 0;
            if (list) Float32(name, (*list)[static_cast<size_t>(i)], bits);
            values[i] = std::bit_cast<float>(bits);
        }
    }
    void ScriptState(const char* name, uint32_t& state)
    {
        uint32_t value = 0;
        if (const json* v = Value(name)) Integer(name, *v, value);
        state = value;
    }

    // The available buildings' catalog positions, then 0 up to the table's size (as the bridge encodes them).
    void AvailableBuildings(int* ids, int count)
    {
        const auto it = in.find("available_buildings");
        const size_t listed = it == in.end() ? 0 : it->size();
        if (listed > static_cast<size_t>(count))
            return Fail("available_buildings", std::to_string(listed) + " over " + std::to_string(count));
        for (int i = 0; i < count; ++i) {
            int32_t id = 0;
            if (static_cast<size_t>(i) < listed) {
                const json& key = (*it)[static_cast<size_t>(i)];
                id = key.is_string() ? context.buildingId(key.get_ref<const std::string&>()) : -1;
                if (id < 0) Fail("available_buildings", "building " + key.dump() + " not in the catalog");
            }
            ids[i] = id;
        }
    }
};

} // namespace

bool ReadWorldJson(const json& in, const ReadContext& context, world::World& world, uint32_t& scriptState, std::string& error)
{
    error.clear();
    if (!in.is_object()) {
        error = "World: not an object";
        return false;
    }
    Reader reader{in, context, error};
    VisitFields(world, scriptState, reader);
    return error.empty();
}

} // namespace obl::world_save
