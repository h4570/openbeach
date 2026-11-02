#include "game/ui/sprite_bank.h"
#include "game/objects/object_static.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/building_construction.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <cstdint>
#include "game/world/world_params.h"

// The building constructor (docs/architecture/objects.md): the logic is src/game/objects/building_construction.h, this is its game Env and the registration.
namespace obl::object {
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

namespace {
using object::Building;

using tables::BeerDef;
using tables::BuildingDef;
using tables::WorldParams;
using ui::SpriteInstance;
namespace cons = building_construction;

test::CallCounter g_counter("objects.building-construct");

class GameEnv {
public:
    const BuildingDef* Definition(const char* key)
    {
        WorldParams* params = GetWorldParams();
        const int id = world::world_params::GetBuildingId(params, key);
        return world::world_params::GetBuildingDef(params, id);
    }
    int32_t DayTimeTicks() { return static_cast<int32_t>(GetWorld()->m_gameDayTime); }
    int Rand() { return game::Rand(); }
    int CommodityFamily(int id) { return world::world_params::GetCommodityFamilyOf(GetWorldParams(), id); }
    int AlcoholFamilyId() { return world::world_params::GetCommodityFamilyId(GetWorldParams(), "ALCOHOL"); }
    const BeerDef* Beer(int id) { return world::world_params::GetBeerDefinition(GetWorldParams(), id); }
    int BeerCount() { return GetWorldParams()->m_numBeers; }
    const char* CommodityDisplayName(int id) { return world::world_params::GetCommodityDisplayName(GetWorldParams(), id); }
    SpriteInstance* CreateSprite(const char* name)
    {
        return (static_cast<ui::SpriteBank*>(ui::GetSpriteBank()))->CreateObservedInstance(name);
    }
};

} // namespace

Building::Building(const char* key) : Prop(key)
{
    g_counter.Count();
    GameEnv env;
    cons::Construct(*this, key, env);
}

} // namespace obl::object
