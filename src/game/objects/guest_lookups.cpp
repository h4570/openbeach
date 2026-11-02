#include "game/world/world_receive.h"
#include "game/ui/sprite.h"
#include "game/map/grid_state.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"
#include "game/map/path_queries.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/objects/guest_shadow.h"
#include "game/objects/guest_conflict.h"
#include "game/objects/guest_lookups.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "game/map/grid_cell.h"
#include "game/ui/map_view.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/map/path_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "game/world/world_script_types.h"
#include "core/call_counter.h"

#include <cstdint>
#include <cstring>
#include "game/ui/map_view_math.h"
#include "game/objects/object_scenery.h"

// The guest functions that look for a place and post a few messages (src/game/objects/guest_lookups.h, docs/architecture/objects.md, place lookups; GetConsumePoint, FindFlirtTarget and MoveActionPerson):
// LookForIllPoint (the first-aid building when it is open, else the hotel, else home), LookForBench (the nearest bench within
// five tiles with a free seat), QueueAtComplaints and QueueAtWharf (a walkable point around the serve position of
// the complaints hut / wharf) and CreateLitter (a litter scenery under the guest, through the world's place command). The script reaches them
// through the guest's CallFunction. An input these functions cannot take stops the game with a fatal error.
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::GetWorld;
using game::Rand;

namespace {

using guest_lookup::HuddleArea;
using guest_shadow::Post;
using object::Building;
using object::Guest;
using object::Scenery;

using IsOpenFunc = bool (*)(const Building*);
using ServeFunc = float* (*)(const Building*, float*);
const IsOpenFunc kIsOpen{&building_api::IsOpen};
const ServeFunc kServe{&building_api::GetServePosition};
using PlayAnimationFunc = float (*)(object::Base*, const char*, int32_t, int32_t, int32_t, const char*);

constexpr const char* kFirstAidName = "FIRSTAID";
constexpr const char* kBenchName = "BENCH";

test::CallCounter g_illPoint("objects.guest-look-for-ill-point");
test::CallCounter g_bench("objects.guest-look-for-bench");
test::CallCounter g_complaints("objects.guest-huddle-complaints-office");
test::CallCounter g_wharf("objects.guest-huddle-wharf");
test::CallCounter g_litter("objects.guest-create-litter");
test::CallCounter g_consume("objects.guest-get-consume-point");
test::CallCounter g_totty("objects.guest-look-for-totty");
test::CallCounter g_moveAction("objects.guest-move-action-person");

void PostToSelf(Guest& guest, int32_t type, int32_t flags, int32_t data)
{
    Post(guest.m_id, static_cast<int32_t>(guest.m_id), type, flags, data);
}

// The first object of that name, or null. The list is the object manager's shared buffer, so the pointer is taken at once.
object::Base* FirstNamed(const char* name)
{
    const auto list = object_manager::FindObjectsByName(GetObjectManager(), name);
    const auto count = static_cast<uint32_t>(list.size());
    return count ? list[0] : nullptr;
}

// The first building with that behavior (the first of the list the name lookup of its key gives), or null.
object::Base* FirstBuilding(BuildingBehavior behavior)
{
    const auto list = obl::object_manager::FindBuildings(*GetObjectManager(), behavior);
    return list.empty() ? nullptr : list.front();
}

// ---- LookForIllPoint ---------------------------------------------------------------------------------------------------------------------

// A random FIRSTAID building (one draw, none when there is no such building) that is open gets the guest (MT_PAVEMENT2OBJECT, flags 1). Otherwise the hotel the
// guest stays in does; with none the guest is leaving early and goes home (MT_STATECHANGE to STATE_GUEST_LEAVING_WORLD).
void IllPoint(Guest& guest)
{
    const auto list = object_manager::FindObjectsByName(GetObjectManager(), kFirstAidName);
    const auto count = static_cast<uint32_t>(list.size());
    if (count != 0) {
        const uint32_t pick = static_cast<uint32_t>(Rand()) % count;
        if (kIsOpen(static_cast<Building*>(list[pick]))) {
            PostToSelf(guest, messages::MT_PAVEMENT2OBJECT, 1, static_cast<int32_t>(list[pick]->m_id));
            return;
        }
    }
    if (guest.m_StayingAt != -1) {
        PostToSelf(guest, messages::MT_PAVEMENT2OBJECT, 1, guest.m_StayingAt);
        return;
    }
    guest.m_leavingEarly = 1;
    PostToSelf(guest, messages::MT_STATECHANGE, 0, guest_lookup::kStateLeavingWorld);
}

// ---- LookForBench ------------------------------------------------------------------------------------------------------------------------

// Nothing while the guest sits (m_HasSatDown). Every bench that is not claimed by a vandal and has a free seat is measured in tiles; asking for the seats
// writes the bench's m_FreeSeat (finding a free scenery seat), so every bench that gets that far has it written, taken or not. The nearest wins (a tie goes to the later one)
// and, with room in it, gets the guest: the bench is told its state, the guest remembers its destination and the bench, takes the bench state and walks to the bench.
void Bench(Guest& guest)
{
    if (guest.m_HasSatDown) return;
    const auto list = object_manager::FindObjectsByName(GetObjectManager(), kBenchName);
    const auto count = static_cast<uint32_t>(list.size());
    if (count == 0) return;
    guest_lookup::NearestBench nearest(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* bench = static_cast<Scenery*>(list[i]);
        if (bench->m_vandalised) continue;
        if (bench->UpdateFreeSeat() == 0) continue;
        if (bench->m_state == guest_lookup::kBenchTaken && bench->UpdateFreeSeat() == 0) continue;
        int here[2], there[2];
        ui::MapView::WorldToTile(here, guest.m_WorldXYZ);
        ui::MapView::WorldToTile(there, bench->m_WorldXYZ);
        nearest.Offer(i, guest_lookup::BenchDistance(here[0], here[1], there[0], there[1]));
    }
    if (nearest.index >= count) return;
    auto* bench = static_cast<Scenery*>(list[nearest.index]);
    if (!guest_lookup::BenchHasRoom(bench->m_NumItems, bench->m_definition->m_Capacity)) return;
    const int32_t benchId = static_cast<int32_t>(bench->m_id);
    Post(guest.m_id, benchId, messages::MT_STATECHANGE, 0, guest_lookup::kBenchTaken);
    guest.m_ActivityPerson = guest.m_DestObjectID;
    guest.m_FightPerson = benchId;
    PostToSelf(guest, messages::MT_STATECHANGE, 0, guest_lookup::kStateBench);
    PostToSelf(guest, messages::MT_PAVEMENT2OBJECT, 0, benchId);
    guest.m_HasSatDown = 1;
}

// ---- QueueAtComplaints / QueueAtWharf ------------------------------------------------------------------------------------------------

// Without the building the game ends with a fatal error.
Building* ComplaintsHut()
{
    return static_cast<Building*>(FirstBuilding(BuildingBehavior::ComplaintsHut));
}

Building* WharfOrStop()
{
    object::Base* found = FirstBuilding(BuildingBehavior::ShipStop);
    return static_cast<Building*>(found ? found : FirstBuilding(BuildingBehavior::BusStop));
}

// The goal is the first fitting point around the serve position: walkpoint 0 (y first, then x) and walkpoint index 0, then the guest walks there on the pavement.
void Huddle(Guest& guest, const HuddleArea& area, Building* building)
{
    float serve[3];
    kServe(building, serve);
    const guest_lookup::HuddleSpot spot = guest_lookup::FindHuddleSpot(
            area, serve[0], serve[1], [] { return Rand(); },
            [&](float x, float y) { return GetGrid()->GetCellAtWorldPoint(x, y)->m_flags; });
    guest.m_walkpoints[0][1] = spot.y;
    guest.m_walkpoints[0][0] = spot.x;
    guest.m_walkpoint = 0;
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, FloatToIntWrap(spot.x), FloatToIntWrap(spot.y));
}

// ---- CreateLitter ------------------------------------------------------------------------------------------------------------------------

// The animation decides the litter (one draw for a drink). A litter is placed on the guest's tile only while the world allows litter (the draw for the
// rotation, a second one, follows the tile); the guest is marked as just having littered (offence state 1, both time stamps now) before the call.
void Litter(Guest& guest)
{
    const char* type = guest_lookup::LitterType(guest.m_CurrAnimation, [] { return Rand(); });
    if (!type) return;
    layout::World* world = GetWorld();
    if (!world->m_allowLitter) return;
    world_script::ScriptPlacement placement{};
    std::strcpy(placement.m_name, type);
    std::strcpy(placement.m_sprite, guest_lookup::kLitterSprite);
    int tile[2];
    ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
    placement.m_x = tile[0];
    placement.m_y = tile[1];
    const uint8_t rotation = static_cast<uint8_t>(Rand() % guest_lookup::kLitterRotations);
    placement.m_layer = 1 | rotation << 8;
    const int32_t unwindMarker = -2;
    std::memcpy(&placement.m_prices[0], &unwindMarker, sizeof(unwindMarker));
    guest.m_justLittered = 1;
    guest.m_timeLittered = world->m_gameTime;
    guest.m_OffenceState = guest_lookup::kLitterOffenceState;
    guest.m_timeOfOffence = world->m_gameTime;
    messages::Message command{-2, -2, 0, 0, 1}; // from the world to the world, the data word is the class (1 = scenery)
    world::PostPlaceCommand(command, &placement);
}

// ---- GetConsumePoint ---------------------------------------------------------------------------------------------------------------------

// A guest in the consume state (0x2d) first looks within five tiles for another guest in that state whose m_ActionState is 2 (waiting); every one found is
// remembered (m_ActionState = 1, m_ActivityPerson = its id) before its point, 20 world units left of it, is checked, so a partner that does not fit still leaves
// both fields written and its point as the start of the random search below. A fitting point is the goal (MT_PAVEMENT2POINT). Any other guest looks at the benches:
// the nearest free one within five tiles (not vandalised, with free seats, not already taken: state 4) is taken one time in ten (the draw is made only when there is
// one), when it has room: the guest is sent to it (m_ActionState 0x14, MT_STATECHANGE to the bench, MT_PAVEMENT2OBJECT to itself). When neither found anything,
// a random point within 250 units of the start (the guest, or the last partner point) that fits becomes the goal; the guest in the consume state forgets the partner.
bool Partner(Guest& guest, guest_lookup::Point3& start)
{
    int tile[2];
    ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
    const auto list = object_manager::GetObjectsNearTile(GetObjectManager(), tile[0], tile[1], guest_lookup::kPartnerRadius);
    const auto count = static_cast<uint32_t>(list.size());
    for (uint32_t i = 0; i < count; ++i) {
        object::Base* other = list[i];
        if (other->m_id == guest.m_id || other->m_type != object::ObjectType::Guest || other->m_state != guest_lookup::kStateConsume ||
                static_cast<Guest*>(other)->m_ActionState != guest_lookup::kPartnerWaiting)
            continue;
        guest.m_ActionState = guest_lookup::kPartnerFound;
        guest.m_ActivityPerson = static_cast<int32_t>(other->m_id);
        start = guest_lookup::PartnerPoint(other->m_WorldXYZ);
        uint32_t cell[2] = {};
        const float world[3] = {start.x, start.y, start.z};
        ui::MapView::WorldToTile(cell, world);
        if (!guest_lookup::ConsumeCellFits(GetGrid()->Cell(cell[0], cell[1]).m_flags)) continue;
        PostToSelf(guest, messages::MT_PAVEMENT2POINT, FloatToIntWrap(start.x), FloatToIntWrap(start.y));
        return true;
    }
    return false;
}

bool ConsumeBench(Guest& guest)
{
    const auto list = object_manager::FindObjectsByName(GetObjectManager(), kBenchName);
    const auto count = static_cast<uint32_t>(list.size());
    if (count == 0) return false;
    guest_lookup::NearestBench nearest(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto* bench = static_cast<Scenery*>(list[i]);
        if (bench->m_vandalised || bench->UpdateFreeSeat() == 0 || bench->m_state == guest_lookup::kBenchTaken) continue;
        int here[2], there[2];
        ui::MapView::WorldToTile(here, guest.m_WorldXYZ);
        ui::MapView::WorldToTile(there, bench->m_WorldXYZ);
        nearest.Offer(i, guest_lookup::BenchDistance(here[0], here[1], there[0], there[1]));
    }
    if (nearest.index >= count || Rand() % guest_lookup::kBenchTripDraw != 0) return false;
    auto* bench = static_cast<Scenery*>(list[nearest.index]);
    if (!guest_lookup::BenchHasRoom(bench->m_NumItems, bench->m_definition->m_Capacity)) return false;
    const int32_t benchId = static_cast<int32_t>(bench->m_id);
    guest.m_ActionState = guest_lookup::kBenchTrip;
    Post(guest.m_id, benchId, messages::MT_STATECHANGE, 0, guest_lookup::kBenchTaken);
    PostToSelf(guest, messages::MT_PAVEMENT2OBJECT, 0, benchId);
    return true;
}

void Consume(Guest& guest)
{
    const bool consuming = guest.m_state == guest_lookup::kStateConsume;
    const guest_lookup::Point3 home{guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    guest_lookup::Point3 start = home;
    if (consuming ? Partner(guest, start) : ConsumeBench(guest)) return;
    map::Grid* grid = GetGrid();
    const guest_lookup::Point3 spot = guest_lookup::FindConsumeSpot(
            start, home, [] { return Rand(); },
            [&](float x, float y, float z) {
                uint32_t cell[2] = {};
                const float world[3] = {x, y, z};
                ui::MapView::WorldToTile(cell, world);
                return grid->Cell(cell[0], cell[1]).m_flags;
            });
    if (consuming) guest.m_ActionState = 0;
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, FloatToIntWrap(spot.x), FloatToIntWrap(spot.y));
}

// ---- FindFlirtTarget ------------------------------------------------------------------------------------------------------------------------

constexpr int kDesireFlirt = 20; // Guest::m_desires

// A male guest looks for a partner: one object drawn from those within two tiles (rand() % count, not a search) is taken when it is another guest, female, in
// one of the two "heading" states and of the taste the guest asks for, and the point beside it (20 units left, as for GetConsumePoint) lies on walkable ground. The
// guest then remembers it (m_ActionState 1, m_ActivityPerson), tells it to stop (MT_MOVESTOP), turns its sprite to the guest, drops its route, changes it to state
// 0x23 and walks to the point. Anything else: no desire to flirt (m_desires[Flirt] not above zero) goes back to searching desires, else the guest walks to a
// tile around the nearest open bar (the search of FindFightPoint). The messages are delivered at once, and the shared object list is read again after each, so
// the partner is `list[pick]` at every use.
bool TottyPartner(Guest& guest, const std::vector<object::Base*>& list, uint32_t pick)
{
    object::Base* other = list[pick];
    if (other->m_id == guest.m_id || other->m_type != object::ObjectType::Guest) return false;
    const tables::GuestDef& theirs = *static_cast<Guest*>(other)->m_definition;
    const guest_lookup::TottySeeker me{guest.m_id, guest.m_definition->m_Gender, guest.m_definition->m_AttractivePref};
    const guest_lookup::TottyPartner partner{
            other->m_id, static_cast<uint8_t>(other->m_type), static_cast<int32_t>(other->m_state), theirs.m_Gender, theirs.m_Attractive};
    if (!guest_lookup::IsTottyPartner(me, partner)) return false;
    const guest_lookup::Point3 spot = guest_lookup::PartnerPoint(other->m_WorldXYZ);
    if (!guest_lookup::ConsumeCellFits(GetGrid()->GetCellAtWorldPoint(spot.x, spot.y)->m_flags)) return false;

    guest.m_ActionState = guest_lookup::kPartnerFound;
    guest.m_ActivityPerson = static_cast<int32_t>(other->m_id);
    Post(guest.m_id, guest.m_ActivityPerson, messages::MT_MOVESTOP, 0, 0);
    const float from[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    object::Base* target = list[pick];
    (static_cast<ui::SpriteInstance*>(target->m_sprite))
            ->FaceTowards(target->m_WorldXYZ[0], target->m_WorldXYZ[1], target->m_WorldXYZ[2], from[0], from[1], from[2]);
    auto* mover = static_cast<Guest*>(list[pick]);
    if (mover->m_pathCursor) {
        path::ReleaseCursor(mover->m_pathCursor);
        static_cast<Guest*>(list[pick])->m_pathCursor = nullptr;
    }
    Post(guest.m_id, guest.m_ActivityPerson, messages::MT_STATECHANGE, 0, guest_lookup::kTottyState);
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, FloatToIntWrap(spot.x), FloatToIntWrap(spot.y));
    return true;
}

void Totty(Guest& guest)
{
    int tile[2];
    ui::MapView::WorldToTile(tile, guest.m_WorldXYZ);
    const auto list = object_manager::GetObjectsNearTile(GetObjectManager(), tile[0], tile[1], guest_lookup::kTottyRadius);
    const auto count = static_cast<uint32_t>(list.size());
    if (count != 0 && TottyPartner(guest, list, static_cast<uint32_t>(Rand()) % count)) return;
    if (!(guest.m_desires[kDesireFlirt] > 0.0f)) {
        PostToSelf(guest, messages::MT_STATECHANGE, 0, guest_conflict::kStateSearchDesire);
        return;
    }
    int32_t tileX = 0, tileY = 0;
    GuestPickBarTile(guest, tileX, tileY);
    const grid_math::WorldPoint world =
            GetGrid()->TileToWorldPoint(static_cast<uint32_t>(tileX), static_cast<uint32_t>(tileY), guest_lookup::kTottyWorldKind);
    PostToSelf(guest, messages::MT_PAVEMENT2POINT, FloatToIntWrap(world.x), FloatToIntWrap(world.y));
}

// ---- MoveActionPerson --------------------------------------------------------------------------------------------------------------------

void PlayHoldHands(object::Base& object, const char* name)
{
    object.PlayAnimation(name, 0, -1, 1, nullptr);
}

// The guest's action person (m_ActivityPerson) takes the sprite direction of the guest (clamped to the directions of its own animation). Unless the guest is in
// state 0x27 it is also put beside the guest by a table of the guest's direction, and when the guest plays one of the hold-hand animations the pair swap hands
// (the guest plays the other one, the person the one the guest left). The person is then moved with the virtual setPosition. A missing
// person is tolerated and does nothing.
void MoveAction(Guest& guest)
{
    object::Manager* manager = GetObjectManager();
    auto* own = static_cast<ui::SpriteInstance*>(guest.m_sprite);
    object::Base* person = object_manager::ObjectById(manager, guest.m_ActivityPerson);
    if (!person) return;
    auto* theirs = static_cast<ui::SpriteInstance*>(person->m_sprite);
    theirs->m_direction = guest_lookup::PartnerDirection(own->m_direction, theirs->m_sprite->m_anims[theirs->m_animIndex]->m_directions);
    int32_t dx = 0, dy = 0;
    if (guest.m_state != guest_lookup::kStateHoldHands) {
        const guest_lookup::HoldHandsRule rule = guest_lookup::HoldHandsFor(static_cast<uint32_t>(own->m_direction));
        dx = rule.dx;
        dy = rule.dy;
        guest_lookup::HandSwap swap{};
        if (guest_lookup::SwapHands(rule, guest.m_CurrAnimation, swap)) {
            PlayHoldHands(guest, swap.guestPlays);
            PlayHoldHands(*object_manager::ObjectById(manager, guest.m_ActivityPerson), swap.partnerPlays);
        }
    }
    const float position[3] = {guest_lookup::PartnerCoordinate(dx, guest.m_WorldXYZ[0]),
            guest_lookup::PartnerCoordinate(dy, guest.m_WorldXYZ[1]), guest.m_WorldXYZ[2]};
    object::Base* target = object_manager::ObjectById(manager, guest.m_ActivityPerson);
    target->SetPosition(position);
}

} // namespace

void Guest::LookForIllPoint()
{
    g_illPoint.Count();
    IllPoint(*this);
}

void Guest::LookForBench()
{
    g_bench.Count();
    Bench(*this);
}

// Without its hut the huddle has no point to walk to: the call stops the game with a fatal error.
void Guest::QueueAtComplaints()
{
    Building* hut = ComplaintsHut();
    if (!hut) fatal::UnhandledInput("objects.guest-huddle-complaints-office", "no complaints hut");
    g_complaints.Count();
    Huddle(*this, guest_lookup::kComplaintsArea, hut);
}

// Without its wharf the huddle has no point to walk to: the call stops the game with a fatal error.
void Guest::QueueAtWharf()
{
    Building* wharf = WharfOrStop();
    if (!wharf) fatal::UnhandledInput("objects.guest-huddle-wharf", "no wharf");
    g_wharf.Count();
    Huddle(*this, guest_lookup::kWharfArea, wharf);
}

void Guest::CreateLitter()
{
    g_litter.Count();
    Litter(*this);
}

void Guest::GetConsumePoint()
{
    g_consume.Count();
    Consume(*this);
}

void Guest::FindFlirtTarget()
{
    g_totty.Count();
    Totty(*this);
}

void Guest::MoveActionPerson()
{
    g_moveAction.Count();
    MoveAction(*this);
}

} // namespace obl::object
