#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"
#include "game/objects/guest.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "core/fatal.h"
#include "game/objects/guest_shadow.h"
#include "game/map/grid_math.h"
#include "game/objects/guest_placement.h"
#include "game/tables/building_behavior.h"
#include "game/objects/object_manager_queries.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"
#include "core/call_counter.h"

#include <cstdint>
#include <vector>

// The small guest functions that put a guest or its partner somewhere (src/game/objects/guest_placement.h, guest placement, docs/architecture/objects.md): SetupSnogOffset
// and FaceEachOther (the kiss), FindLodging (the room), SetSurfacePoint (the sea tile a
// windsurfer sets out for) and IsEntryPointClear (called by the guest's GetValue). The script reaches them through
// the guest's CallFunction; none of them sends a message. SetupSnogOffset and SetSurfacePoint move an object through its virtual SetPosition/Teleport, which
// also re-files it in the grid's cell lists.
namespace obl::object {
using game::GetGrid;
using game::GetObjectManager;
using game::Rand;

namespace {

using object::Building;
using object::Guest;
using ui::SpriteInstance;
namespace om = obl::object_manager;

using BuildingQuery = bool (*)(const Building*);
const BuildingQuery kHasBuildJob{&building_api::HasBuildJob};
const BuildingQuery kHasDemolishJob{&building_api::HasDemolishJob};
const BuildingQuery kIsOpen{&building_api::IsOpen};

test::CallCounter g_snog("objects.guest-setup-snog-offset");
test::CallCounter g_face("objects.guest-face-each-other");
test::CallCounter g_stay("objects.guest-find-some-where-to-stay");
test::CallCounter g_surface("objects.guest-set-surface-point");
test::CallCounter g_entry("objects.guest-is-entry-point-clear");

SpriteInstance* SpriteOf(object::Base& object)
{
    return static_cast<SpriteInstance*>(object.m_sprite);
}

void SetPosition(object::Base& object, const float (&position)[3])
{
    object.SetPosition(position);
}

// ---- SetupSnogOffset -------------------------------------------------------------------------------------------------------

// The partner the guest kisses is moved a fixed step from the guest, by the direction of the guest's sprite. A missing sprite or partner, or a
// direction outside the step table, ends in UnhandledInput.
bool SnogPlan(Guest& guest, object::Base*& partner, float (&position)[3])
{
    const SpriteInstance* sprite = SpriteOf(guest);
    if (!sprite) return false;
    guest_placement::SnogStep step{};
    if (!guest_placement::SnogStepFor(static_cast<uint32_t>(sprite->m_direction), step)) return false;
    partner = object_manager::ObjectById(GetObjectManager(), guest.m_ActivityPerson);
    if (!partner) return false;
    position[0] = guest_placement::SnogCoordinate(step.x, guest.m_WorldXYZ[0]);
    position[1] = guest_placement::SnogCoordinate(step.y, guest.m_WorldXYZ[1]);
    position[2] = guest.m_WorldXYZ[2];
    return true;
}

bool Snog(Guest& guest)
{
    object::Base* partner = nullptr;
    float position[3];
    if (!SnogPlan(guest, partner, position)) return false;
    SetPosition(*partner, position);
    return true;
}

// ---- FaceEachOther ---------------------------------------------------------------------------------------------------------

// The two sprites turn to each other, the partner's first (turning the sprite towards the target: target point, then the point the sprite stands at). The positions are
// read before either turns.
bool Face(Guest& guest)
{
    object::Base* partner = object_manager::ObjectById(GetObjectManager(), guest.m_ActivityPerson);
    if (!partner || !partner->m_sprite || !guest.m_sprite) return false;
    const float mine[3] = {guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_WorldXYZ[2]};
    const float theirs[3] = {partner->m_WorldXYZ[0], partner->m_WorldXYZ[1], partner->m_WorldXYZ[2]};
    (SpriteOf(*partner))->FaceTowards(mine[0], mine[1], mine[2], theirs[0], theirs[1], theirs[2]);
    (SpriteOf(guest))->FaceTowards(theirs[0], theirs[1], theirs[2], mine[0], mine[1], mine[2]);
    return true;
}

// ---- FindLodging ---------------------------------------------------------------------------------------------------

bool HasRoom(const Building& building)
{
    return guest_placement::HasRoom(building.m_definition->m_GuestCapacity, building.m_NumGuests);
}

void Claim(Guest& guest, Building& building)
{
    guest.m_StayingAt = static_cast<int32_t>(building.m_id);
    ++building.m_NumGuests;
}

// The room is asked of the first entry of the HOTEL list; with no hotel the call ends in UnhandledInput (the guard in this function
// keeps it from reading past an empty list).
bool StayDeclined(const Guest& guest)
{
    if (guest.m_NightsToStay == 0) return false;
    return om::FindBuildings(*GetObjectManager(), BuildingBehavior::Hotel).empty();
}

// The first hotel with room takes the guest; otherwise the first open accommodation block with room and no build or demolish job. A guest who finds
// nothing stays no nights and leaves at 21:00.
void Stay(Guest& guest)
{
    guest.m_StayingAt = -1;
    if (guest.m_NightsToStay != 0) {
        object::Manager* manager = GetObjectManager();
        const auto hotels = om::FindBuildings(*manager, BuildingBehavior::Hotel);
        auto* hotel = hotels.empty() ? nullptr : static_cast<Building*>(hotels.front());
        if (hotel && HasRoom(*hotel)) {
            Claim(guest, *hotel);
        } else {
            for (object::Base* candidate : om::FindBuildings(*manager, BuildingBehavior::AccomBlock)) {
                auto* block = static_cast<Building*>(candidate);
                if (!HasRoom(*block) || kHasBuildJob(block) || kHasDemolishJob(block) || !kIsOpen(block)) continue;
                Claim(guest, *block);
                break;
            }
        }
    }
    if (guest.m_StayingAt == -1) {
        guest.m_NightsToStay = 0;
        guest.m_TimeToLeave = guest_placement::kNoRoomLeaveTime;
    }
}

// ---- SetSurfacePoint -------------------------------------------------------------------------------------------------------

// Random points around the guest (up to its swim distance either way, x then y, two draws each) until one lies on a water tile, then the guest is
// teleported there at its own height. The loop has no bound: a call that finds no water never returns.
void Surface(Guest& guest)
{
    map::Grid* grid = GetGrid();
    const float z = guest.m_WorldXYZ[2];
    for (;;) {
        const guest_placement::SurfaceCandidate point = guest_placement::SurfaceCandidateAround(
                guest.m_WorldXYZ[0], guest.m_WorldXYZ[1], guest.m_definition->m_SwimDist, [] { return Rand(); });
        const grid_math::TileXY tile = grid->WorldPointToTile(point.x, point.y);
        if ((grid->Cell(tile.x, tile.y).m_flags & guest_placement::kTileWater) == 0) continue;
        guest.Teleport(point.x, point.y, z);
        return;
    }
}

// ---- IsEntryPointClear -----------------------------------------------------------------------------------------------------

bool Entry(const Guest& guest)
{
    map::Grid* grid = GetGrid();
    const grid_math::TileXY tile = grid->WorldPointToTile(guest.m_walkpoints[0][0], guest.m_walkpoints[0][1]);
    return guest_placement::EntryPointClear(*grid, tile.x, tile.y);
}

// Water tiles of the real grid (flag 4): where a windsurfer's call can end.
std::vector<grid_math::WorldPoint> WaterCentres(int limit)
{
    std::vector<grid_math::WorldPoint> centres;
    map::Grid* grid = GetGrid();
    for (uint32_t ty = 0; ty < grid->m_height && static_cast<int>(centres.size()) < limit; ty += 3)
        for (uint32_t tx = 0; tx < grid->m_width && static_cast<int>(centres.size()) < limit; tx += 3) {
            if ((grid->Cell(tx, ty).m_flags & guest_placement::kTileWater) == 0) continue;
            const grid_math::WorldPoint centre = grid_math::TileToWorldPoint(grid->m_tileSize, tx, ty, 4);
            const grid_math::TileXY tile = grid->WorldPointToTile(centre.x, centre.y);
            if (tile.x == tx && tile.y == ty) centres.push_back(centre);
        }
    return centres;
}

} // namespace

void Guest::SetupSnogOffset()
{
    if (!Snog(*this)) fatal::UnhandledInput("objects.guest-setup-snog-offset", "no sprite, step or partner");
    g_snog.Count();
}

void Guest::FaceEachOther()
{
    if (!Face(*this)) fatal::UnhandledInput("objects.guest-face-each-other", "no partner or sprite");
    g_face.Count();
}

void Guest::FindLodging()
{
    if (StayDeclined(*this))
        fatal::UnhandledInput("objects.guest-find-some-where-to-stay", "no hotel: the old code read a stale list entry");
    g_stay.Count();
    Stay(*this);
}

void Guest::SetSurfacePoint()
{
    g_surface.Count();
    Surface(*this);
}

// A few calls of each function on the first two guests, through the handlers (so they reach the call counters): natural play kisses nobody, windsurfs nowhere and asks for an entry point only through a script value (tests/guest.txt).
void ForceGuestPlacement()
{
    Guest* guest = guest_shadow::GuestAt(0);
    Guest* other = guest_shadow::GuestAt(1);
    if (!guest || !other || !SpriteOf(*guest) || !SpriteOf(*other)) return;
    guest_shadow::GuestBackup backup(guest);
    SpriteInstance* mine = SpriteOf(*guest);
    SpriteInstance* theirs = SpriteOf(*other);
    const SpriteInstance savedMine = *mine, savedTheirs = *theirs;
    const float home[3] = {other->m_WorldXYZ[0], other->m_WorldXYZ[1], other->m_WorldXYZ[2]};
    const float spot[3] = {guest->m_WorldXYZ[0], guest->m_WorldXYZ[1], guest->m_WorldXYZ[2]};
    guest->m_ActivityPerson = static_cast<int32_t>(other->m_id);
    for (int direction = 0; direction < 8; ++direction) {
        mine->m_direction = direction;
        guest->SetupSnogOffset();
        SetPosition(*other, home);
        guest->FaceEachOther();
    }
    *mine = savedMine;
    *theirs = savedTheirs;
    GuestIsEntryPointClear(*guest);
    const std::vector<grid_math::WorldPoint> water = WaterCentres(1);
    if (!water.empty()) {
        const float sea[3] = {water[0].x, water[0].y, 0.0f};
        SetPosition(*guest, sea);
        guest->SetSurfacePoint();
        SetPosition(*guest, spot);
    }
}

bool GuestIsEntryPointClear(Guest& guest)
{
    g_entry.Count();
    return Entry(guest);
}

} // namespace obl::object
