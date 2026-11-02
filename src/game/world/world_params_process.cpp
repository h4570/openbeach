// WorldParams Construct / Destroy: the object's start and end of life. Construct loads the converted catalog (with the footprint
// grids) and the converted guest set that the scenario names; the records own their data, so Destroy only deletes the object. The
// contents of the tables are checked by the worldparams-dump oracle (tests/worldparams_layout*.txt).

#include "core/pack_path.h"
#include "game/world/world_params.h"
#include "game/game/diagnostics.h"

#include "game/game/game_accessors.h"
#include "core/call_counter.h"
#include "game/tables/world_params_reset.h"
#include "game/tables/catalog_load.h"
#include "game/tables/guest_set_load.h"
#include "game/objects/guest_sets.h"

#include <cstdint>
#include <string>

namespace obl::world {
namespace {

using tables::WorldParams;
namespace wpp = world_params_process;

void Fatal(const char* format, const char* argument)
{
    game::FatalError(format, argument);
}

// Lifecycle calls reported by the test interpreter.
test::CallCounter g_constructCalls{"world.params-construct"};
test::CallCounter g_destructCalls{"world.params-destruct"};

WorldParams* g_current = nullptr; // world_params::Current()

} // namespace

// Zero, catalog (footprints included), then the guest set the scenario names by its set name (`guests` in scenario.json, e.g. campaign_01).
// The .ble files are not read: the converted packs hold their data (game-data.md, "Converted pack"). A Fatal on a missing pack ends
// the process.
tables::WorldParams* world_params::Construct(const char* guestList)
{
    g_constructCalls.Count();
    auto* params = new WorldParams{};
    g_current = params;
    wpp::Reset(*params, wpp::Beers::Zero);
    std::string error;
    if (!catalog::LoadPack(pack_path::Selected(), *params, error)) {
        Fatal("Building catalog: %s. Import the game data again", error.c_str());
        return nullptr;
    }
    const char* set = guest_sets::SetForName(guestList);
    if (!set && guest_sets::IsPackSetName(guestList)) set = guestList; // an own pack names its set freely
    if (!set) {
        Fatal("Unrecognized guest list: %s", guestList);
        return nullptr;
    }
    if (!guest_set::LoadPack(pack_path::Selected(), set, *params, error)) {
        Fatal("Guest set: %s. Import the game data again", error.c_str());
        return nullptr;
    }
    return params;
}

void world_params::Destroy(tables::WorldParams* params)
{
    g_destructCalls.Count();
    if (g_current == params) g_current = nullptr;
    delete params;
}

tables::WorldParams* world_params::Current()
{
    return g_current;
}

} // namespace obl::world
