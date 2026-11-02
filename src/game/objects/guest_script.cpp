#include "game/ui/sprite_bank.h"
#include "game/ui/sound_api.h"
#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "game/world/world_queries.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"
#include "game/objects/guest_script.h"
#include "game/world/world_queries_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/guest_call_table.h"
#include "game/objects/guest_script_functions.h"
#include "game/objects/guest_script_values.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_building.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/ui/text_table.h"
#include "game/objects/commodity_providers.h"
#include "game/world/world_params.h"
#include "game/game/game_api.h"
#include "game/game/diagnostics.h"
#include "game/objects/script_value_convert.h"
#include "game/objects/building_events_state.h"
#include "game/objects/object_scenery.h"

#include <cstdint>
#include <cstring>
#include <iterator>

namespace obl::object {
using game::FatalError;
using game::GetCommodityProviders;
using game::GetGrid;
using game::GetObjectManager;
using game::GetWeather;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

// A delegated id that the desire trace probes runs through its seam, so the trace sees the call the script makes.
template<auto Seam> void Guest::RunThroughSeam()
{
    (g_guestSeams.*Seam)(this);
}

namespace {

using object::Building;
using object::Guest;
using script::ScriptValue;
using ui::SpriteInstance;
namespace gs = guest_script;

// Call counters (`call-report <name>`).
test::CallCounter g_getValueCalls("objects.guest-get-value");
test::CallCounter g_setValueCalls("objects.guest-set-value");
// The `Set` numbers that reach the sprite bank or a bin: counted apart so a test can see the world-hash runs go through them.
test::CallCounter g_setValueExternalCalls("objects.guest-set-value-external");
test::CallCounter g_callFunctionCalls("objects.guest-call-function");
test::CallCounter g_callInlineCalls("objects.guest-call-function-inline"); // the cases with their own body
test::CallCounter g_callDelegatedCalls("objects.guest-call-function-delegated"); // the cases handed on to a guest function

// ---- The game side of the Env of guest_script_values.h -----------------------------------------------------------------------------

constexpr auto kReleaseSprite = [](auto* owner, void* instance) { static_cast<ui::SpriteBank*>(owner)->ReleaseInstance(instance); };
constexpr auto kCreateSprite = [](auto* owner, auto... args) {
    return static_cast<ui::SpriteBank*>(owner)->CreateObservedInstance(args...);
};

// The entry of the guest function each delegated id hands the guest to, in the order of guest_call_table.h (the ids the unit test
// pins). The ones the guest-desire trace probes go through g_guestSeams.
struct DelegateEntry {
    uint8_t id;
    void (Guest::*entry)();
};
constexpr DelegateEntry kDelegateEntries[] = {
        {0x00, &Guest::RunThroughSeam<&GuestSeams::updateDesires>},
        {0x01, &Guest::RunThroughSeam<&GuestSeams::updateOpinions>},
        {0x02, &Guest::RunThroughSeam<&GuestSeams::updateDrowning>},
        {0x03, &Guest::SearchDesire},
        {0x04, &Guest::RunThroughSeam<&GuestSeams::decideSwimAction>},
        {0x05, &Guest::AlterDesires},
        {0x06, &Guest::GetConsumePoint},
        {0x07, &Guest::RandomizePalette},
        {0x0e, &Guest::GotoDancePoint},
        {0x0f, &Guest::RunThroughSeam<&GuestSeams::findFightPoint>},
        {0x10, &Guest::RunThroughSeam<&GuestSeams::findFightPerson>},
        {0x11, &Guest::CreateLitter},
        {0x12, &Guest::SetSurfacePoint},
        {0x14, &Guest::RandomizeAttachedPalette},
        {0x15, &Guest::FindFlirtTarget},
        {0x18, &Guest::MoveActionPerson},
        {0x19, &Guest::FaceEachOther},
        {0x1a, &Guest::HeadForBin},
        {0x1b, &Guest::FindLodging},
        {0x1f, &Guest::MissedDesire},
        {0x22, &Guest::RunThroughSeam<&GuestSeams::alterPriceOpinion>},
        {0x23, &Guest::RunThroughSeam<&GuestSeams::alterQueueOpinion>},
        {0x24, &Guest::RunThroughSeam<&GuestSeams::alterSafetyOpinion>},
        {0x26, &Guest::SurfToShore},
        {0x29, &Guest::GetNextPoolPoint},
        {0x2a, &Guest::RunThroughSeam<&GuestSeams::lookForVandalItem>},
        {0x2b, &Guest::RunThroughSeam<&GuestSeams::vandaliseBin>},
        {0x2e, &Guest::LookForBench},
        {0x31, &Guest::LookForIllPoint},
        {0x32, &Guest::WalkAlongPath},
        {0x33, &Guest::WanderBeach},
        {0x35, &Guest::QueueAtComplaints},
        {0x36, &Guest::ChooseFunSlideAction},
        {0x37, &Guest::SetupFunPoolSwim},
        {0x3b, &Guest::RunThroughSeam<&GuestSeams::doSpotOpinion>},
        {0x3c, &Guest::RunThroughSeam<&GuestSeams::sendOutFightingMsgs>},
        {0x3d, &Guest::GenerateBlockedMessage},
        {0x3e, &Guest::GetSeaEntry},
        {0x3f, &Guest::TellNearbyOfTopless},
        {0x40, &Guest::StartPathWalk},
        {0x41, &Guest::WindSurfStraight},
        {0x42, &Guest::RunThroughSeam<&GuestSeams::updatePurchaseOpinion>},
        {0x43, &Guest::RunThroughSeam<&GuestSeams::updatePriceScore>},
        {0x45, &Guest::WalkToWindsurf},
        {0x47, &Guest::SendOutDeathMessage},
        {0x4c, &Guest::SetupStartValues},
        {0x4d, &Guest::DemolishedAsleep},
        {0x4e, &Guest::QueueAtWharf},
        {0x4f, &Guest::AddDrowningThought},
        {0x50, &Guest::NoRouteToLodging},
        {0x52, &Guest::AddBuildingFullThought},
        {0x53, &Guest::SetupSnogOffset},
        {0x55, &Guest::AddThoughtIDidnt},
        {0x56, &Guest::AddThoughtIDidIt},
        {0x57, &Guest::AddCommodBeachClosed},
        {0x58, &Guest::HeadForEntryPoint},
        {0x59, &Guest::GotoWorldUnloadPoint},
        {0x5b, &Guest::AddIllPurchaseThought},
};

constexpr bool SameIds()
{
    if (std::size(kDelegateEntries) != std::size(kGuestCallDelegateIds)) return false;
    for (size_t i = 0; i < std::size(kDelegateEntries); ++i)
        if (kDelegateEntries[i].id != kGuestCallDelegateIds[i]) return false;
    return true;
}
static_assert(SameIds(), "kDelegateEntries must list kGuestCallDelegateIds");

void (Guest::* g_delegates[gs::kClearDelayedFunction + 1])() = {};

map::Grid* GridInstance()
{
    return GetGrid();
}

class GameEnv {
public:
    object::Base* Object(int32_t id) const { return object_manager::ObjectById(GetObjectManager(), id); }
    object::Base* const* FindBuildings(BuildingBehavior behavior, uint32_t& count) const
    {
        m_found = obl::object_manager::FindBuildings(*GetObjectManager(), behavior);
        count = static_cast<uint32_t>(m_found.size());
        return m_found.data();
    }
    float SlotPrice(object::Base* building, int32_t slot) const
    {
        return static_cast<float>(commodity_providers::SlotPrice(static_cast<Building*>(building), slot));
    }
    float SlotReferencePrice(object::Base* building, int32_t slot) const
    {
        return static_cast<float>(commodity_providers::SlotReferencePrice(static_cast<Building*>(building), slot));
    }
    const char* CommodityName(int32_t commodity) const { return world::world_params::GetCommodityName(GetWorldParams(), commodity); }
    int32_t CommodityFamilyOf(int32_t commodity) const { return world::world_params::GetCommodityFamilyOf(GetWorldParams(), commodity); }
    const char* FamilyKey(int32_t family) const { return GetWorldParams()->m_commodityFamilies[family].m_key.c_str(); }
    int32_t ActiveEventId(object::BuildingEvents* events) const { return events->GetActiveEventId(); }
    int32_t SeaArea(float x, float y, float z) const { return GridInstance()->GetSeaAreaOfPoint(x, y, z); }
    uint32_t CellFlags(float x, float y, float z) const { return GridInstance()->GetCellAtWorldPoint(x, y)->m_flags; }
    bool SlotUsable(int32_t building, int32_t slot) const
    {
        return commodity_providers::IsSlotUsable(GetCommodityProviders(), building, slot);
    }
    bool NeighboursClear(Guest& guest) const { return GuestIsEntryPointClear(guest); }
    float GameDeltaTime() const { return GetWorld()->m_gameDeltaTime; }
    float GameTime() const { return GetWorld()->m_gameTime; }
    ScriptValue Base(Guest& guest, int32_t key, intptr_t data) const
    {
        ScriptValue out{};
        guest.Person::GetValue(&out, key, data);
        return out;
    }
    void Base(Guest& guest, int32_t key, intptr_t data, int32_t type, int32_t bits) const { guest.Person::SetValue(key, data, type, bits); }
    // The guest's CallFunction
    uint32_t Delegate(Guest& guest, uint32_t id) const
    {
        (guest.*g_delegates[id])();
        return 0; // Guest::CallFunction ignores the callee's return value
    }
    void InvalidFunction() const { FatalError("A script called a function guests do not have"); }
    void FaceTowards(SpriteInstance* sprite, const float* target, const float* from) const
    {
        (sprite)->FaceTowards(target[0], target[1], target[2], from[0], from[1], from[2]);
    }
    void PlaySound3D(const char* name, float* position) const { audio::CurrentSound()->Play3D(name, position, 1, -1); }
    const float* FindSunbathePoint(Guest& guest) const { return g_guestSeams.findSunbathePoint(&guest); }
    const float* FindSeaPoint(Guest& guest, const float* position) const { return GuestFindNearestSeaPoint(position[0], position[1]); }
    void FindRandomWalkPoint(float* out) const { world_data::World::RandomWalkPoint(out); }
    int32_t TextId(const char* key) const { return static_cast<int32_t>(ui::text_table::FindKey(key)); }
    void Post(const messages::Message& message) const
    {
        messages::Message copy = message;
        messages::message_facade::Post(messages::message_facade::Current(), &copy);
    }
    void PostDelayed(const messages::Message& message, float delay) const
    {
        messages::Message copy = message;
        messages::message_facade::PostDelayed(messages::message_facade::Current(), &copy, delay);
    }
    void ClearDelayed(int32_t receiver) const
    {
        messages::message_facade::ClearDelayedForReceiver(messages::message_facade::Current(), receiver);
    }
    int32_t DayLengthSeconds() const
    {
        int32_t seconds = 0;
        game::game_parameters::GetInt(game::game_parameters::Current(), &seconds, "iDAY_LENGTH_IN_SECONDS");
        return seconds;
    }
    float GameDayTime() const { return GetWorld()->m_gameDayTime; }
    uint32_t WeatherState() const { return GetWeather()->m_currentState; }
    void AddShag() const { ++GetWorld()->m_shagCount; }
    // A float converts to int through script_value::ToInt, the same conversion every script value uses.
    int32_t FloatToIntWrap(int32_t floatBits) const { return script_value::ToInt(gs::kFloat, static_cast<uint32_t>(floatBits)); }
    int Rand() const { return game::Rand(); }
    void SetBinFill(object::Base* bin, int32_t fill) const { static_cast<object::Scenery*>(bin)->SetBinFill(static_cast<uint32_t>(fill)); }
    void ReleaseSprite(void* sprite) const { kReleaseSprite(ui::GetSpriteBank(), sprite); }
    void* CreateSprite(const char* name) const { return kCreateSprite(ui::GetSpriteBank(), name); }
    void SetAnim(void* sprite, const char* name, float startTime, float* position) const
    {
        (static_cast<SpriteInstance*>(sprite))->SetAnimByName(name, startTime, position);
    }

private:
    mutable object_manager::ObjectList m_found; // the last FindBuildings result, read through the pointer it returned
};

} // namespace

bool RegisterGuestScriptDelegates()
{
    for (const DelegateEntry& delegate : kDelegateEntries) g_delegates[delegate.id] = delegate.entry;
    return true;
}

// The script's Get/Set/Call entries of the guest. Each counts its call before the work; the first ScriptValue output is returned
// through the pointer the caller passed.
script::ScriptValue* Guest::GetValue(script::ScriptValue* out, int key, intptr_t data)
{
    g_getValueCalls.Count();
    GameEnv env;
    *out = gs::GetValue(*this, key, data, env);
    return out;
}

void Guest::SetValue(int key, intptr_t data, int type, int bits)
{
    g_setValueCalls.Count();
    if (gs::SetsExternal(key)) g_setValueExternalCalls.Count();
    GameEnv env;
    gs::SetValue(*this, key, data, type, bits, env);
}

void Guest::CallFunction(int id)
{
    g_callFunctionCalls.Count();
    (gs::Delegated(static_cast<uint32_t>(id)) ? g_callDelegatedCalls : g_callInlineCalls).Count();
    GameEnv env;
    gs::CallFunction(*this, static_cast<uint32_t>(id), env);
}

} // namespace obl::object
