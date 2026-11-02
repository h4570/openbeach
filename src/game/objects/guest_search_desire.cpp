#include "game/objects/guest.h"
#include "game/ui/sprite.h"
#include "game/world/world_queries_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/map_view_math.h"
#include "core/fatal.h"
#include "game/objects/guest_search_desire.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/ui/text_table.h"
#include "game/objects/commodity_providers.h"
#include "game/world/world_params.h"
#include "game/game/diagnostics.h"

#include <cstdint>

// SearchDesire (delegate 0x03 of the guest script; src/game/objects/guest_search_desire.h, docs/architecture/objects.md "objects.guest-search-desire"): the guest
// picks its next desire. The decision is the template in the common header; this file is its game side (the environment that calls the other guest
// functions, commodity providers, text table and message handler).
namespace obl::object {
using game::FatalError;
using game::GetCommodityProviders;
using game::GetWeather;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

namespace {

using object::Building;
using object::Guest;
using tables::BuildingDef;
using ui::SpriteInstance;

using PlayAnimationFunc = float (*)(object::Base*, const char*, int32_t, int32_t, int32_t, const char*);

test::CallCounter g_search("objects.guest-search-desire");

guest_search::Provider& ProviderGlobals()
{
    // The function's two static globals : zero at start, never reset between scenarios.
    static guest_search::Provider provider{};
    return provider;
}

struct GameEnv {
    Guest& guest;
    float walkPoint[3] = {};

    int32_t Weather() const { return static_cast<int32_t>(GetWeather()->m_currentState); }
    float GameDeltaTime() const { return GetWorld()->m_gameDeltaTime; }
    float DayTime() const { return GetWorld()->m_gameDayTime; }
    bool StartStomachIllness() { return g_guestSeams.startBadGuts(&guest); }
    bool CheckLeaveResort() { return g_guestSeams.checkLeaveResort(&guest); }
    bool CheckBedTime() { return g_guestSeams.checkBedTime(&guest); }
    int Rand() { return game::Rand(); }
    int32_t FindKey(const char* key) { return static_cast<int32_t>(ui::text_table::FindKey(key)); }
    // Read the key stored in the selected commodity family record.
    const char* RetryFamilyName()
    {
        tables::WorldParams* params = GetWorldParams();
        const BuildingDef* def = world::world_params::GetBuildingDef(params, guest.m_CurTypeID);
        const int family = world::world_params::GetCommodityFamilyOf(params, def->m_Commodity[guest.m_desired_commod]);
        return params->m_commodityFamilies[family].m_key.c_str();
    }
    guest_search::Provider& Provider() { return ProviderGlobals(); }
    int32_t FindProvider(
            int32_t* slot, uint8_t* found, int32_t* broken, int32_t* closed, int32_t* beachClosed, uint8_t* retry, int32_t family)
    {
        int tile[2] = {};
        int* at = ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
        return commodity_providers::FindProvider(GetCommodityProviders(), reinterpret_cast<int*>(slot), reinterpret_cast<char*>(found),
                reinterpret_cast<int*>(broken), reinterpret_cast<int*>(closed), reinterpret_cast<int*>(beachClosed),
                reinterpret_cast<char*>(retry), guest.m_definition->m_Gender, family, at, guest.m_CurID, static_cast<int>(guest.m_id));
    }
    // A provider id that is no object is not checked (providers are always live buildings).
    const BuildingDef& DefinitionOf(int32_t id) { return *static_cast<Building*>(FindObjectById(id))->m_definition; }
    const char* BuildingKey(int32_t id) { return DefinitionOf(id).m_key.c_str(); }
    const char* CommodityName(int32_t id, int32_t slot) { return GuestCommodityName(DefinitionOf(id), slot); }
    int32_t BuildingTypeId(int32_t id) { return DefinitionOf(id).m_id; }
    void WalkAlongPath() { guest.WalkAlongPath(); }
    void PlayWalkAnimation() { guest.PlayAnimation("WalkAnimation", 0, -1, 1, nullptr); }
    void BadVisitSound() { GuestBadVisitSound(guest); }
    const float* FindSunbathePoint() { return g_guestSeams.findSunbathePoint(&guest); }
    const float* FindSwimPoint(float x, float y, float z) { return g_guestSeams.findSwimPoint(x, y, z); }
    const float* RandomWalkPoint() { return layout::World::RandomWalkPoint(walkPoint); }
    bool SouvenirShopAllowed()
    {
        const int id = world::world_params::GetBuildingId(GetWorldParams(), "SOUVSHOP");
        return GetWorld()->BuildingAllowed(id);
    }
    void SetRepeatAction(int32_t desire, float value) { g_guestSeams.setRepeatAction(&guest, desire, value); }
    void Fatal(const char* name) { FatalError("Guest \"%s\" has no desire to walk to", name); }
    void Post(const messages::Message& message)
    {
        messages::Message copy = message;
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    void ApplyWalkAnimation()
    {
        (static_cast<SpriteInstance*>(guest.m_sprite))->SetAnimByName(guest.m_WalkAnimation, GetWorld()->m_gameTime, guest.m_WorldXYZ);
    }
};

bool Search(Guest& guest)
{
    GameEnv env{guest};
    return guest_search::SearchDesire(guest, env);
}

} // namespace

void Guest::SearchDesire()
{
    // A guest without a definition, a sprite or a mood icon is a broken object, not a decision: nothing to do.
    if (!m_definition || !m_sprite || !m_moodIcon)
        fatal::UnhandledInput("objects.guest-search-desire", "the guest has no definition, sprite or mood icon");
    g_search.Count();
    Search(*this);
}

} // namespace obl::object
