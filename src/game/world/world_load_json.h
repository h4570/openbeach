#pragma once

#include "formats/save_states.h"

#include "game/world/world_state.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// The `world` section of an obl.save read into the live World (docs/architecture/data-formats/save-switch.md): the
// inverse of WriteWorldJson over the same world_save::VisitFields. Every field LoadWorld reads gets its value, a missing
// optional key the bridge's default (0, false, empty text, no available buildings), so a native load and a bridge load of
// one document leave the same World.
namespace obl::world_save {

struct ReadContext {
    formats::SaveObjectStates objectStates; // the pack's state names, for `base.state`
    // UTF-8 text (the map file) in the code page of the pack's language; false when a character has no byte there.
    std::function<bool(std::string_view, std::string&)> utf8ToNarrow;
    // The catalog position of a building key (the catalog's `buildings` list); -1 when the catalog has none.
    std::function<int32_t(std::string_view)> buildingId;
    // The catalog positions of a beer and an event key (the `beers` and `events` lists); -1 when the catalog has none.
    std::function<int32_t(std::string_view)> beerId;
    std::function<int32_t(std::string_view)> eventId;
};

// Fills the World's saved fields and `scriptState` (LoadWorld hands it to WorldScriptSetState); false with `error` naming
// the key on a value the section cannot hold. The fields LoadWorld does not read keep their values.
bool ReadWorldJson(const nlohmann::json& in, const ReadContext& context, world::World& world, uint32_t& scriptState, std::string& error);

} // namespace obl::world_save
