#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "game/objects/script_text.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "core/float_to_int.h"
#include "game/objects/guest_shadow.h"
#include "game/world/world_params.h"
#include "game/objects/guest_conflict.h"
#include "game/ui/map_view.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <vector>
#include "game/ui/map_view_math.h"
#include "game/objects/object_static.h"

// The guest fights (FindFightPoint, FindFightPerson, TellNearbyOfFight) and tells the men around of
// topless sunbathing (TellNearbyOfTopless), guest fights and vandalism, and guest walking (docs/architecture/objects.md).
// They send immediate messages (the guest's own script, a neighbour's).
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::GetWorldParams;
using game::Rand;

namespace {

using object::Building;
using object::Guest;
using namespace guest_conflict;
using guest_shadow::Post;
using guest_shadow::PostDelayed;

using IsOpenFunc = bool (*)(const Building*);
const IsOpenFunc kIsOpen{&building_api::IsOpen};

constexpr int kDesireVandalise = 18, kDesireFight = 19; // Guest::m_desires

test::CallCounter g_fightPoint("objects.guest-find-fight-point");
test::CallCounter g_fightPerson("objects.guest-find-fight-person");
test::CallCounter g_fightMsgs("objects.guest-send-out-fighting-msgs");
test::CallCounter g_toplessMsgs("objects.guest-send-out-topless-messages");
test::CallCounter g_lookForVandal("objects.guest-look-for-vandal-item");
test::CallCounter g_vandaliseBin("objects.guest-vandalise-bin");

// ---- FindFightPoint ----------------------------------------------------------------------------------------------------

// The walkable tile around the nearest open bar: FindFightPoint's search below its desire test, which FindFlirtTarget runs as well (GuestPickBarTile).
FightTile PickBarTile(Guest& guest)
{
    tables::WorldParams* params = GetWorldParams();
    const int family = world::world_params::GetCommodityFamilyId(params, "ALCOHOL");
    const auto buildings = object_manager::GetListByClass(GetObjectManager(), static_cast<int>(object::ObjectType::Building));
    const auto count = static_cast<uint32_t>(buildings.size());
    int mine[2] = {};
    ui::MapView::WorldToTile(mine, guest.m_WorldXYZ);
    NearestBar nearest;
    for (uint32_t i = 0; i < count; ++i) {
        auto* building = static_cast<Building*>(buildings[i]);
        if (!kIsOpen(building)) continue;
        const tables::BuildingDef* def = building->m_definition;
        const int* commodities = def->m_Commodity.data();
        for (uint32_t slot = 0; slot < static_cast<uint32_t>(def->m_NumCommodities); ++slot) {
            if (world::world_params::GetCommodityFamilyOf(params, commodities[slot]) != family) continue;
            int theirs[2] = {};
            ui::MapView::WorldToTile(theirs, building->m_WorldXYZ);
            nearest.Offer(i, BarDistance(mine[0] - theirs[0], mine[1] - theirs[1]));
        }
    }
    // With no open bar at all the first building of the list is used (how many there are is not checked).
    int bar[2] = {};
    ui::MapView::WorldToTile(bar, buildings[nearest.index]->m_WorldXYZ);
    map::Grid* grid = GetGrid();
    return PickFightTile(
            bar[0], bar[1], [] { return Rand(); },
            [&](int32_t x, int32_t y) { return grid->Cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).m_flags; });
}

void RunFindFightPoint(Guest& guest)
{
    const int32_t id = static_cast<int32_t>(guest.m_id);
    // Not above zero: a desire of zero, below zero or NaN sends the guest back to its desires.
    if (!(guest.m_desires[kDesireFight] > 0.0f)) {
        Post(guest.m_id, id, messages::MT_STATECHANGE, 0, kStateSearchDesire);
        return;
    }
    const FightTile tile = PickBarTile(guest);
    const grid_math::WorldPoint origin = GetGrid()->TileToWorldOrigin(static_cast<uint32_t>(tile.x), static_cast<uint32_t>(tile.y));
    Post(guest.m_id, id, messages::MT_PAVEMENT2POINT, FloatToIntWrap(origin.x), FloatToIntWrap(origin.y));
}

// ---- FindFightPerson ---------------------------------------------------------------------------------------------------

void RunFindFightPerson(Guest& guest)
{
    int tile[2] = {};
    ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
    const auto list = object_manager::GetObjectsNearTile(GetObjectManager(), tile[0], tile[1], kPersonRadius);
    const auto count = static_cast<uint32_t>(list.size());
    map::Grid* grid = GetGrid();
    for (uint32_t i = 0; i < count; ++i) {
        object::Base* other = list[i];
        if (other->m_id == guest.m_id || other->m_type != object::ObjectType::Guest) continue;
        if (static_cast<Guest*>(other)->m_definition->m_Gender != guest.m_definition->m_Gender) continue;
        if (!CanBeFought(other->m_state)) continue;
        // The spot beside the opponent: the tile is read there, and the guest walks to it.
        const float spot[3] = {OpponentApproachX(other->m_WorldXYZ[0]), other->m_WorldXYZ[1], other->m_WorldXYZ[2]};
        uint32_t cell[2] = {};
        ui::MapView::WorldToTile(cell, spot);
        if (!WalkableTile(grid->Cell(cell[0], cell[1]).m_flags)) continue;

        guest.m_ActionState = 1;
        const int32_t opponent = static_cast<int32_t>(list[i]->m_id);
        guest.m_FightPerson = opponent;
        Post(guest.m_id, opponent, messages::MT_MOVESTOP, 0, 0);
        // The first delivery may have changed the guest (the receiver is its FightPerson now).
        Post(guest.m_id, guest.m_FightPerson, messages::MT_STATECHANGE, 0, kStateFighting);
        const float from[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
        object::Base* target = list[i];
        (static_cast<ui::SpriteInstance*>(target->m_sprite))
                ->FaceTowards(target->m_WorldXYZ[0], target->m_WorldXYZ[1], target->m_WorldXYZ[2], from[0], from[1], from[2]);
        Post(guest.m_id, static_cast<int32_t>(guest.m_id), messages::MT_MOVE2POINT, FloatToIntWrap(spot[0]), FloatToIntWrap(spot[1]));
        return; // only the first opponent
    }
}

// ---- TellNearbyOfFight -----------------------------------------------------------------------------------------------

void RunSendOutFightingMsgs(Guest& guest)
{
    int tile[2] = {};
    ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
    const auto list = object_manager::GetObjectsNearTile(GetObjectManager(), tile[0], tile[1], kWitnessRadius);
    const auto count = static_cast<uint32_t>(list.size());
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i]->m_type != object::ObjectType::Guest) continue;
        Post(guest.m_id, static_cast<int32_t>(list[i]->m_id), messages::MT_SPOT_OPINION, 0, kSpotFight);
    }
    const int32_t id = static_cast<int32_t>(guest.m_id);
    PostDelayed(guest.m_id, id, messages::MT_LOOK_AROUND, 0, 0, kLookAroundDelay);
}

// ---- TellNearbyOfTopless --------------------------------------------------------------------------------------------

// Like TellNearbyOfFight, but it tells only the men of the neighbourhood (the sender too, when it is a man).
void RunSendOutToplessMessages(Guest& guest)
{
    int tile[2] = {};
    ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
    const auto list = object_manager::GetObjectsNearTile(GetObjectManager(), tile[0], tile[1], kToplessRadius);
    const auto count = static_cast<uint32_t>(list.size());
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i]->m_type != object::ObjectType::Guest) continue;
        if (!IsToplessWitness(static_cast<Guest*>(list[i])->m_definition->m_Gender)) continue;
        Post(guest.m_id, static_cast<int32_t>(list[i]->m_id), messages::MT_SPOT_OPINION, 0, kSpotTopless);
    }
}

// ---- LookForVandalItem -------------------------------------------------------------------------------------------------

void RunLookForVandalItem(Guest& guest)
{
    int mine[2] = {};
    ui::MapView::WorldToTile(mine, guest.m_WorldXYZ);
    VandalSearch search;
    object::Scenery* chosen = nullptr;
    static const char* const kNames[3] = {"Bin", "Bench", "STREET_LAMP"};
    for (int32_t kind = Bin; kind <= StreetLamp; ++kind) {
        const auto list = object_manager::FindObjectsByName(GetObjectManager(), kNames[kind]);
        const auto count = static_cast<uint32_t>(list.size());
        for (uint32_t i = 0; i < count; ++i) {
            auto* object = static_cast<object::Scenery*>(list[i]);
            int theirs[2] = {};
            ui::MapView::WorldToTile(theirs, object->m_WorldXYZ);
            const int32_t metric = VandalMetric(mine[0] - theirs[0], mine[1] - theirs[1]);
            if (!search.Reaches(metric)) continue;
            if (!VandalTargetFree(object->m_state, object->m_vandalised, object->m_cleaning, object->m_repairing)) continue;
            search.Take(metric, kind);
            chosen = object;
        }
    }
    if (!chosen) {
        guest.m_desires[kDesireFight] = VandalisedToFight(guest.m_desires[kDesireFight], guest.m_desires[kDesireVandalise]);
        guest.m_desires[kDesireVandalise] = 0.0f;
        guest.m_ActionState = kActionStateIdle;
        guest.m_ActivityPerson = -1;
        return;
    }
    // The offset tables are indexed with the sprite direction and end at 7: any other direction ends in UnhandledInput.
    if (static_cast<uint32_t>(static_cast<ui::SpriteInstance*>(chosen->m_sprite)->m_direction) >= kVandalDirections) {
        fatal::UnhandledInput("objects.guest-look-for-vandal-item", "the sprite direction is past the offset tables");
    }
    const int32_t id = static_cast<int32_t>(guest.m_id);
    Post(guest.m_id, static_cast<int32_t>(chosen->m_id), messages::MT_STATECHANGE, 0, kStateVandalised);
    // Direction and position are read after the delivery.
    const VandalOffset offset =
            VandalStandingOffset(search.kind, static_cast<uint32_t>(static_cast<ui::SpriteInstance*>(chosen->m_sprite)->m_direction));
    Post(guest.m_id, id, messages::MT_MOVE2POINT,
            FloatToIntWrap(static_cast<double>(chosen->m_WorldXYZ[0]) + static_cast<double>(offset.a)),
            FloatToIntWrap(static_cast<double>(chosen->m_WorldXYZ[1]) + static_cast<double>(offset.b)));
    guest.m_ActivityPerson = static_cast<int32_t>(chosen->m_id);
    guest.m_ActionState = VandalActionState(search.kind, chosen->m_NumItems == chosen->m_definition->m_Capacity);
}

// ---- VandaliseBin ------------------------------------------------------------------------------------------------------

constexpr char kAnimFullbinVandalise[] = "FULLBIN_VANDALISE";
constexpr char kAnimEmptybinVandalise[] = "EMPTYBIN_VANDALISE";
constexpr char kAnimBenchVandalise[] = "BENCH_VANDALISE";
constexpr char kAnimStreetlampVandalise1[] = "STREETLAMP_VANDALISE_1";
constexpr char kAnimStreetlampVandalise2[] = "STREETLAMP_VANDALISE_2";

void RunVandaliseBin(Guest& guest)
{
    object::Manager* manager = GetObjectManager();
    if (!object_manager::ObjectById(manager, guest.m_ActivityPerson)) return;
    object::Base* target = object_manager::ObjectById(manager, guest.m_ActivityPerson);
    const float from[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    static_cast<ui::SpriteInstance*>(guest.m_sprite)
            ->FaceTowards(target->m_WorldXYZ[0], target->m_WorldXYZ[1], target->m_WorldXYZ[2], from[0], from[1], from[2]);
    static_cast<object::Scenery*>(object_manager::ObjectById(manager, guest.m_ActivityPerson))->SetVandalClaim(1);
    // The animation by ActionState 2..6; any other state sends a message of zeros. The message carries the name's script::text id
    // (the receiver plays it by name), so the names are statics that outlive the delivery.
    int32_t animation = 0;
    bool known = true;
    switch (guest.m_ActionState) {
    case 2: animation = script::text::Id(kAnimFullbinVandalise); break;
    case 3: animation = script::text::Id(kAnimEmptybinVandalise); break;
    case 4: animation = script::text::Id(kAnimBenchVandalise); break;
    case 5: animation = script::text::Id(kAnimStreetlampVandalise1); break;
    case 6: animation = script::text::Id(kAnimStreetlampVandalise2); break;
    default: known = false; break;
    }
    if (known)
        Post(guest.m_id, guest.m_ActivityPerson, messages::MT_PLAYANIMATION, 0, animation);
    else
        Post(0, 0, 0, 0, 0);
    guest.m_OffenceState = kOffenceVandalism;
    guest.m_timeOfOffence = GetWorld()->m_gameTime;
}

// ---- entries ------------------------------------------------------------------------------------------------------------------------

bool HasWalkableNear(object::Base* building)
{
    int bar[2] = {};
    ui::MapView::WorldToTile(bar, building->m_WorldXYZ);
    map::Grid* grid = GetGrid();
    for (int dy = -kBarRadius; dy <= kBarRadius; ++dy)
        for (int dx = -kBarRadius; dx <= kBarRadius; ++dx) {
            const int x = bar[0] + dx, y = bar[1] + dy;
            if (x >= 0 && x < static_cast<int>(kGridTiles) && y >= 0 && y < static_cast<int>(kGridTiles) &&
                    WalkableTile(grid->Cell(static_cast<uint32_t>(x), static_cast<uint32_t>(y)).m_flags))
                return true;
        }
    return false;
}

} // namespace

void Guest::FindFightPoint()
{
    g_fightPoint.Count();
    RunFindFightPoint(*this);
}

void Guest::FindFightPerson()
{
    g_fightPerson.Count();
    RunFindFightPerson(*this);
}

void Guest::TellNearbyOfFight()
{
    g_fightMsgs.Count();
    RunSendOutFightingMsgs(*this);
}

void Guest::TellNearbyOfTopless()
{
    g_toplessMsgs.Count();
    RunSendOutToplessMessages(*this);
}

void Guest::LookForVandalItem()
{
    g_lookForVandal.Count();
    RunLookForVandalItem(*this);
}

void Guest::VandaliseBin()
{
    g_vandaliseBin.Count();
    RunVandaliseBin(*this);
}

void GuestPickBarTile(object::Guest& guest, int32_t& x, int32_t& y)
{
    const FightTile tile = PickBarTile(guest);
    x = tile.x;
    y = tile.y;
}

bool GuestBarGroundWalkable(object::Base* building)
{
    return HasWalkableNear(building);
}

} // namespace obl::object
