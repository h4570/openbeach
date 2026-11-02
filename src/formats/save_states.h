#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// The named states of obl.save (docs/architecture/data-formats/saves.md, "Named states"), shared by the game and the converter.
// A value outside its set stays a number, so a value the game never stores (an old save's stale word) survives a round trip.
namespace obl::formats {

enum class SaveStateSet : uint8_t {
    None,
    Weather,
    BuildingWear,
    BuildingOpen,
    RepairJob,
    BuilderStatus,
    RouteType,
    VehicleType,
    ObjectState
};

// The set of a field: an object key (`building.open_status`) or a section key (`Weather.state`).
SaveStateSet SaveStateSetOf(std::string_view field);
// The names of a fixed set in value order; empty for None and ObjectState, whose names come from the pack.
std::span<const std::string_view> SaveStateNames(SaveStateSet set);

// The pack's object states (scripts/object_vocabulary.json), by id.
using SaveObjectStates = std::map<int32_t, std::string, std::less<>>;

// The document value of a state: its name, or the number when the set does not name it.
nlohmann::ordered_json SaveStateValue(SaveStateSet set, int32_t value, const SaveObjectStates* objectStates = nullptr);
// The number of a document value (a name of the set or a plain integer); nullopt for anything else.
std::optional<int32_t> SaveStateNumber(SaveStateSet set, const nlohmann::json& value, const SaveObjectStates* objectStates = nullptr);
std::optional<int32_t> SaveStateNumber(
        SaveStateSet set, const nlohmann::ordered_json& value, const SaveObjectStates* objectStates = nullptr);

} // namespace obl::formats
