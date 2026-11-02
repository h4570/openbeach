#include "formats/save_states.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

namespace obl::formats {
namespace {

constexpr std::string_view kWeather[] = {"heat_wave", "normal", "overcast", "light_rain", "heavy_rain", "thunderstorm"};
constexpr std::string_view kBuildingWear[] = {"sound", "worn", "very_worn", "broken"};
constexpr std::string_view kBuildingOpen[] = {"open", "closed"};
constexpr std::string_view kRepairJob[] = {"inspection", "scenery", "breakdown", "watering"};
constexpr std::string_view kBuilderStatus[] = {"available", "unavailable"};
constexpr std::string_view kRouteType[] = {"pavement", "land", "shallow_or_land", "sea", "land_or_blocked"};
constexpr std::string_view kVehicleType[] = {"jetski", "speedboat", "ferry", "banana", "pedaloe", "coach", "fishing_boat"};

constexpr std::pair<std::string_view, SaveStateSet> kFields[] = {
        {"Weather.state", SaveStateSet::Weather},
        {"Weather.previous_state", SaveStateSet::Weather},
        {"building.repair_state", SaveStateSet::BuildingWear},
        {"building.open_status", SaveStateSet::BuildingOpen},
        {"RepairManager.type", SaveStateSet::RepairJob},
        {"ConstructionManager.status", SaveStateSet::BuilderStatus},
        {"moving.path.type", SaveStateSet::RouteType},
        {"vehicle.vehicle_type", SaveStateSet::VehicleType},
        {"base.state", SaveStateSet::ObjectState},
};

template<class Json> std::optional<int32_t> Number(SaveStateSet set, const Json& value, const SaveObjectStates* objectStates)
{
    if (value.is_number_integer()) return static_cast<int32_t>(value.template get<int64_t>());
    if (!value.is_string()) return std::nullopt;
    const std::string& name = value.template get_ref<const std::string&>();
    if (set == SaveStateSet::ObjectState) {
        if (objectStates)
            for (const auto& [id, state] : *objectStates)
                if (state == name) return id;
        return std::nullopt;
    }
    const auto names = SaveStateNames(set);
    const auto it = std::find(names.begin(), names.end(), name);
    if (it == names.end()) return std::nullopt;
    return static_cast<int32_t>(it - names.begin());
}

} // namespace

SaveStateSet SaveStateSetOf(std::string_view field)
{
    for (const auto& [name, set] : kFields)
        if (name == field) return set;
    return SaveStateSet::None;
}

std::span<const std::string_view> SaveStateNames(SaveStateSet set)
{
    switch (set) {
    case SaveStateSet::Weather: return kWeather;
    case SaveStateSet::BuildingWear: return kBuildingWear;
    case SaveStateSet::BuildingOpen: return kBuildingOpen;
    case SaveStateSet::RepairJob: return kRepairJob;
    case SaveStateSet::BuilderStatus: return kBuilderStatus;
    case SaveStateSet::RouteType: return kRouteType;
    case SaveStateSet::VehicleType: return kVehicleType;
    default: return {};
    }
}

nlohmann::ordered_json SaveStateValue(SaveStateSet set, int32_t value, const SaveObjectStates* objectStates)
{
    if (set == SaveStateSet::ObjectState) {
        if (objectStates)
            if (const auto it = objectStates->find(value); it != objectStates->end()) return it->second;
        return value;
    }
    const auto names = SaveStateNames(set);
    if (value >= 0 && static_cast<size_t>(value) < names.size()) return std::string(names[static_cast<size_t>(value)]);
    return value;
}

std::optional<int32_t> SaveStateNumber(SaveStateSet set, const nlohmann::json& value, const SaveObjectStates* objectStates)
{
    return Number(set, value, objectStates);
}

std::optional<int32_t> SaveStateNumber(SaveStateSet set, const nlohmann::ordered_json& value, const SaveObjectStates* objectStates)
{
    return Number(set, value, objectStates);
}

} // namespace obl::formats
