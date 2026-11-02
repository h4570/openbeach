#pragma once

#include "formats/save_states.h"

#include "game/world/world_state.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// The `world` section of an obl.save written from the live World (docs/architecture/data-formats/save-switch.md):
// world_save::VisitFields with the obl.save keys instead of the binary records. The keys and value rules are those of the
// converter's mapping (kWorldFields in importer/src/readers/save/save_writer.cpp); while the game still maps its binary records, the game's
// writer checks that both give the same section.
namespace obl::world_save {

struct JsonContext {
    formats::SaveObjectStates objectStates; // the pack's state names, for `base.state`
    // Old narrow text (the map file) in UTF-8, in the code page of the pack's language.
    std::function<std::string(std::string_view)> narrowToUtf8;
    // The catalog key of a building id (its position in the catalog's `buildings` list); empty when there is none.
    std::function<std::string(int32_t)> buildingKey;
    // The catalog keys of a beer and an event id (positions in the catalog's `beers` and `events` lists); empty when there is none.
    std::function<std::string(int32_t)> beerKey;
    std::function<std::string(int32_t)> eventKey;
    // Whether the ConstructionManager has the building under construction or demolition (the building save asks it).
    std::function<bool(int32_t)> underConstruction;
    std::function<bool(int32_t)> underDemolition;
};

// The obl.save key of a VisitFields record (the record's name, world_save.h): null for a dropped record and for the valid-building pair,
// which the visitors write and read as `available_buildings`. `name` is one of VisitFields' literals (null-terminated).
const char* WorldJsonKey(std::string_view name);

// Writes the section into `out` (an object, replaced); false with `error` naming the field on a value obl.save cannot hold.
bool WriteWorldJson(
        const world::World& world, uint32_t scriptState, const JsonContext& context, nlohmann::ordered_json& out, std::string& error);

} // namespace obl::world_save
