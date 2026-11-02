#include "formats/save_records.h"

#include "core/json_schema.h"
#include "formats/cell_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obl::formats {
namespace {

using nlohmann::json;
using nlohmann::ordered_json;

enum class Unused : uint8_t { Zero, FloatZero, None, Text };

struct Column {
    const char* slots; // the fixed array's key; null: the rule's key
    const char* field; // its key in the record; null: the list holds the values themselves
    Unused unused;
};

// A list of records (or plain values) with its trailing unused entries left out.
struct ListRule {
    const char* parent; // the object holding the arrays; "[]" walks a list
    const char* key;
    size_t count;
    std::span<const Column> columns;
};

// An array of floats, one per name, as a record.
struct RecordRule {
    const char* parent;
    const char* key;
    std::span<const char* const> names;
};

constexpr const char* kPreferences[] = {"alcohol", "amusement", "meals", "party_fun", "snacks", "soft_drinks", "souvenirs", "sports_fun",
        "sunbathe", "swimming", "water_fun", "stroll", "sleep"};
constexpr const char* kOpinions[] = {"beauty", "crowding", "attractiveness", "price", "variety", "cleanliness", "safety"};
constexpr const char* kTolerances[] = {"beauty_low", "beauty_high", "crowding_low", "crowding_high", "attractiveness_low",
        "attractiveness_high", "price_low", "price_high", "variety_low", "variety_high", "cleanliness_low", "cleanliness_high",
        "safety_low", "safety_high"};
constexpr const char* kDesires[] = {"accommodation", "alcohol", "amusement", "first_aid", "headache", "meals", "party_fun", "shower",
        "snacks", "soft_drinks", "souvenirs", "sports_fun", "sunbathe", "swim_fun", "toilet", "water_sports_fun", "stroll", "leave",
        "vandalise", "fight", "flirt", "umbrellas", "suntan", "intimacy", "sleep", "scared"};
constexpr const char* kXY[] = {"x", "y"};

constexpr RecordRule kRecords[] = {
        {"objects[].guest", "person", kPreferences},
        {"objects[].guest", "opinion", kOpinions},
        {"objects[].guest", "tolerance", kTolerances},
        {"objects[].guest", "desires", kDesires},
        {"managers.weather", "desire_modifiers", kDesires},
        {"view", "position", kXY},
        {"view", "velocity", kXY},
};

constexpr Column kStaffSlots[] = {{"staff", "id", Unused::Zero}, {"staff_busy", "busy", Unused::Zero}};
constexpr Column kVehicleSlots[] = {{"vehicles", "id", Unused::Zero}, {"vehicle_in_use", "in_use", Unused::Zero}};
constexpr Column kCommodities[] = {{"commodity_intensity_male", "intensity_male", Unused::FloatZero},
        {"commodity_intensity_female", "intensity_female", Unused::FloatZero}, {"commodity_price", "price", Unused::FloatZero},
        {"commodity_price_factor", "price_factor", Unused::FloatZero}, {"commodity_cost", "cost", Unused::FloatZero}};
constexpr Column kBoatGuests[] = {
        {"guest_keywords", "keyword", Unused::Text}, {"guest_counts", "count", Unused::Zero}, {"guest_random", "random", Unused::Zero}};
constexpr Column kNumbers[] = {{nullptr, nullptr, Unused::Zero}};
constexpr Column kReferences[] = {{nullptr, nullptr, Unused::None}};
constexpr Column kFloats[] = {{nullptr, nullptr, Unused::FloatZero}};
constexpr Column kTexts[] = {{nullptr, nullptr, Unused::Text}};

constexpr ListRule kLists[] = {
        {"objects[].building", "staff", 10, kStaffSlots},
        {"objects[].building", "vehicles", 6, kVehicleSlots},
        {"objects[].building", "commodities", 5, kCommodities},
        {"objects[].building", "non_serve_staff", 10, kNumbers},
        {"objects[].building", "tables", 10, kNumbers},
        {"objects[].building", "loungers", 20, kNumbers},
        {"objects[].building", "profit_history", 64, kFloats},
        {"objects[].vehicle", "guests", 300, kNumbers},
        {"objects[].guest", "inventory", 5, kNumbers},
        {"objects[].guest", "thoughts", 3, kReferences},
        {"objects[].scenery", "attached_guests", 2, kReferences},
        {"objects[].staff", "thoughts", 3, kTexts},
        {"managers.construction.jobs[]", "work_point_owners", 10, kReferences},
        {"managers.ferry.boats[]", "guests_by_type", 20, kBoatGuests},
        {"managers.ferry.performances[]", "guests_by_type", 20, kBoatGuests},
        {"managers.ferry.departures", "average_happiness", 20, kFloats},
        {"managers.feedback", "suppressed", 100, kNumbers},
        {"managers.commodities", "complaints", 26, kNumbers},
};

// Calls `visit` for each object `path` reaches; a missing key ends that branch.
template<class Json, class Visit> void EachParent(Json& at, std::string_view path, const Visit& visit)
{
    if (!at.is_object()) return;
    if (path.empty()) return visit(at);
    const size_t end = std::min(path.find('.'), path.find("[]"));
    const auto it = at.find(std::string(path.substr(0, end)));
    if (it == at.end()) return;
    std::string_view rest = end == std::string_view::npos ? std::string_view() : path.substr(end);
    const bool list = rest.starts_with("[]");
    if (list) rest.remove_prefix(2);
    if (rest.starts_with(".")) rest.remove_prefix(1);
    if (!list) return EachParent(*it, rest, visit);
    if (it->is_array())
        for (Json& element : *it) EachParent(element, rest, visit);
}

template<class Json> Json UnusedValue(Unused unused)
{
    switch (unused) {
    case Unused::Zero: return 0;
    case Unused::FloatZero: return 0.0;
    case Unused::None: return -1;
    default: return "";
    }
}

// -0.0 and other bit patterns are values of their own, so the test is on the written form.
bool IsUnused(const ordered_json& value, Unused unused)
{
    return value.dump() == UnusedValue<ordered_json>(unused).dump();
}

void ToList(ordered_json& parent, const ListRule& rule)
{
    const bool plain = rule.columns.front().field == nullptr;
    std::vector<ordered_json> arrays;
    bool any = false;
    for (const Column& c : rule.columns) {
        const auto it = parent.find(c.slots ? c.slots : rule.key);
        const bool found = it != parent.end() && it->is_array();
        arrays.push_back(found ? *it : ordered_json::array());
        any = any || found;
        if (it != parent.end()) parent.erase(it);
    }
    if (!any) return;
    ordered_json list = ordered_json::array();
    size_t used = 0;
    for (size_t i = 0; i < rule.count; ++i) {
        ordered_json entry = plain ? ordered_json() : ordered_json::object();
        bool unused = true;
        for (size_t c = 0; c < rule.columns.size(); ++c) {
            if (i >= arrays[c].size()) continue;
            const ordered_json& value = arrays[c][i];
            unused = unused && IsUnused(value, rule.columns[c].unused);
            if (plain)
                entry = value;
            else
                entry[rule.columns[c].field] = value;
        }
        list.push_back(std::move(entry));
        if (!unused) used = i + 1;
    }
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(used), list.end());
    parent[rule.key] = std::move(list);
}

void ToSlots(json& parent, const ListRule& rule)
{
    const bool plain = rule.columns.front().field == nullptr;
    const auto it = parent.find(rule.key);
    json list = json::array();
    if (it != parent.end()) {
        list = std::move(*it);
        parent.erase(it);
    }
    for (const Column& c : rule.columns) {
        json values = json::array();
        for (size_t i = 0; i < rule.count; ++i) {
            const json* value = nullptr;
            if (list.is_array() && i < list.size()) {
                const json& entry = list[i];
                if (plain)
                    value = &entry;
                else if (const auto field = entry.find(c.field); field != entry.end())
                    value = &*field;
            }
            values.push_back(value ? *value : UnusedValue<json>(c.unused));
        }
        parent[c.slots ? c.slots : rule.key] = std::move(values);
    }
}

void FillMissing(json& value, const json& node, const json& root);

// The node a local "$ref" names; other references have no object fields.
const json& Target(const json& node, const json& root)
{
    const auto ref = node.find("$ref");
    if (ref == node.end()) return node;
    const std::string& path = ref->get_ref<const std::string&>();
    return path.starts_with("#/$defs/") ? root["$defs"][path.substr(8)] : node;
}

void FillProperties(json& object, const json& node, const json& root)
{
    const auto properties = node.find("properties");
    if (properties == node.end()) return;
    for (const auto& [key, field] : properties->items()) {
        auto it = object.find(key);
        if (it == object.end()) {
            const auto fallback = field.find("default");
            if (fallback == field.end()) continue; // optional without a default: absent has a meaning of its own
            it = object.emplace(key, *fallback).first;
        }
        FillMissing(*it, field, root);
    }
}

void FillMissing(json& value, const json& node, const json& root)
{
    const json& target = Target(node, root);
    if (value.is_object()) {
        FillProperties(value, target, root);
    } else if (value.is_array()) {
        const auto items = target.find("items");
        if (items == target.end()) return;
        for (json& element : value) FillMissing(element, *items, root);
    }
}

// The state blocks of an object besides `sprite` and `base`.
std::vector<std::string> BlocksOf(const json& object)
{
    const std::string kind = object.at("kind");
    if (kind == "building" || kind == "scenery" || kind == "table") return {"static", kind};
    if (kind == "animal") return {"moving", "shark"};
    if (kind == "guest" || kind == "vehicle" || kind == "performer") return {"moving", kind};
    const std::string objectClass = object.value("class", "");
    if (objectClass.empty()) return {"moving", "staff"}; // the loader refuses staff without a class
    return {"moving", "staff", objectClass == "waiter" ? "service" : objectClass};
}

} // namespace

void FillSaveDefaults(json& document, const json_schema::Registry& schemas)
{
    const json* root = schemas.Find("obl.save");
    if (!root) return;
    const json& node = (*root)["$defs"]["object"];
    for (json& object : document["objects"]) {
        for (const std::string& block : BlocksOf(object))
            if (!object.contains(block)) object[block] = json::object();
        FillProperties(object, node, *root);
        // A building keeps its rotation in the object's `dir` instead, so the field has no default of its own.
        if (!object.contains("dir") && !object["sprite"].contains("dir")) object["sprite"]["dir"] = 0;
        if (object["kind"] == "building") // a building's queue and events always belong to it
            for (const char* part : {"queue", "events"})
                if (const auto it = object["building"].find(part); it != object["building"].end() && (*it)["owner"] == -1)
                    (*it)["owner"] = object["id"];
    }
}

void SlotsToRecords(ordered_json& document)
{
    for (const RecordRule& rule : kRecords)
        EachParent(document, rule.parent, [&](ordered_json& parent) {
            const auto it = parent.find(rule.key);
            if (it == parent.end() || !it->is_array()) return;
            ordered_json record = ordered_json::object();
            for (size_t i = 0; i < rule.names.size() && i < it->size(); ++i) record[rule.names[i]] = (*it)[i];
            *it = std::move(record);
        });
    for (const ListRule& rule : kLists) EachParent(document, rule.parent, [&](ordered_json& parent) { ToList(parent, rule); });
    if (const auto map = document.find("map"); map != document.end() && map->contains("flags")) {
        (*map)["cell_state"] = CellWordsToRuns((*map)["flags"]);
        map->erase("flags");
    }
}

void RecordsToSlots(json& document)
{
    for (const RecordRule& rule : kRecords)
        EachParent(document, rule.parent, [&](json& parent) {
            const auto it = parent.find(rule.key);
            if (it != parent.end() && !it->is_object()) return;
            json slots = json::array();
            for (const char* name : rule.names) {
                const bool present = it != parent.end() && it->contains(name);
                slots.push_back(present ? (*it)[name] : json(0.0));
            }
            parent[rule.key] = std::move(slots);
        });
    for (const ListRule& rule : kLists) EachParent(document, rule.parent, [&](json& parent) { ToSlots(parent, rule); });
    if (const auto map = document.find("map"); map != document.end() && map->contains("cell_state")) {
        (*map)["flags"] = CellRunsToWords((*map)["cell_state"]);
        map->erase("cell_state");
    }
}

} // namespace obl::formats
