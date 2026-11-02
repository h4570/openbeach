#include "game/map/grid_state.h"
#include "game/objects/staff_queries.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/objects/staff_roster.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/staff_lookup.h"
#include "core/call_counter.h"

#include <cstdint>
#include "game/ui/map_view_math.h"

namespace obl::object {
using game::GetGrid;
using game::Rand;

namespace {

using map::GridCell;
using object::Staff;
using staff::StateKind;

// The grid as the game reads it: the cell under a world point (the cell at the world position) for the neighbour search, the tile of the map view world-to-tile conversion
// for the wander search; both end in the flags of the cell, the second one wrapping the tile like the grid does.
struct GameQueryEnv {
    int Rand() { return game::Rand(); }
    uint32_t CellFlags(float x, float y, float z) { return GetGrid()->GetCellAtWorldPoint(x, y)->m_flags; }
    uint32_t TileFlags(float x, float y, float z)
    {
        const float point[3] = {x, y, z};
        int tile[2];
        ui::MapView::WorldToTile(tile, point);
        return GetGrid()->Cell(static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1])).m_flags;
    }
};

test::CallCounter g_walkable("objects.staff-find-walkable-point-near");
test::CallCounter g_wander("objects.staff-find-wander-pos");
test::CallCounter g_home("objects.staff-set-home");
test::CallCounter g_reduce("objects.staff-reduce-selection-to-hover");

// Track HasState calls separately for each profession; the index identifies its query adapter.
struct StateSlot {
    StateKind kind;
    test::CallCounter counter;
    StateSlot(StateKind kindIn, const char* name) : kind(kindIn), counter(name) {}
};

StateSlot g_state[] = {
        {StateKind::Cleaner, "objects.cleaner-has-state"},
        {StateKind::Lifeguard, "objects.lifeguard-has-state"},
        {StateKind::Mechanic, "objects.mechanic-has-state"},
        {StateKind::Rep, "objects.rep-has-state"},
        {StateKind::Security, "objects.security-guard-has-state"},
        {StateKind::Builder, "objects.builder-has-state"},
};

} // namespace

namespace staff_facade {

// The result is the return value and `pos`; neither the member nor the grid is written.
bool FindWalkablePointNear(Staff*, float* pos, int dx, int dy)
{
    g_walkable.Count();
    GameQueryEnv env;
    return staff::FindWalkablePointNear(pos, dx, dy, env);
}

void FindWanderPos(Staff* employee)
{
    g_wander.Count();
    GameQueryEnv env;
    staff::FindWanderPos(*employee, env);
}

void SetHome(Staff* employee, uint32_t homeId)
{
    g_home.Count();
    staff::SetHome(*employee, homeId);
}

void ReduceSelectionToHover(Staff* employee)
{
    g_reduce.Count();
    staff::ReduceSelectionToHover(*employee);
}

// A pure function of the state number (the object is not read).
template<int I> bool HasState(Staff*, uint32_t state)
{
    StateSlot& slot = g_state[I];
    slot.counter.Count();
    return staff::HasState(slot.kind, state);
}

} // namespace staff_facade
} // namespace obl::object

namespace obl::object {
void Staff::ReduceSelectionToHover()
{
    object::staff_facade::ReduceSelectionToHover(this);
}
void Staff::SetHome(int32_t id)
{
    object::staff_facade::SetHome(this, id);
}
bool Builder::HasState(uint32_t state)
{
    return object::staff_facade::HasState<5>(this, state);
}
bool Cleaner::HasState(uint32_t state)
{
    return object::staff_facade::HasState<0>(this, state);
}
bool Lifeguard::HasState(uint32_t state)
{
    return object::staff_facade::HasState<1>(this, state);
}
bool Mechanic::HasState(uint32_t state)
{
    return object::staff_facade::HasState<2>(this, state);
}
bool Rep::HasState(uint32_t state)
{
    return object::staff_facade::HasState<3>(this, state);
}
bool SecurityGuard::HasState(uint32_t state)
{
    return object::staff_facade::HasState<4>(this, state);
}
} // namespace obl::object
