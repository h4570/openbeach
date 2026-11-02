// Behavior of the small helpers the state headers define (no size or offset contracts: saves are field-wise).
#include "game/map/grid_state.h"
#include "game/game/game.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_base.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "game/world/world_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "unit_test.h"

#include <array>
#include <memory>

using namespace obl::world_data;
using enum obl::messages::MessageType;
using obl::messages::kMessageTypeCount;

using obl::map::Grid;

namespace {
struct ProbeState : obl::game::GameState {
    ProbeState() : GameState("Probe") {}
    int Update() override { return 0; }
    void Enter() override {}
};
} // namespace

OBL_TEST_CASE("state_helpers.current_state_index")
{
    obl::game::Game game{};
    for (auto& state : game.m_states) state = std::make_unique<ProbeState>();
    OBL_CHECK_EQ(obl::game::CurrentStateIndex(game), -1);
    game.m_currentState = game.m_states[static_cast<int>(obl::game::GameStateId::LoadGame)].get();
    OBL_CHECK_EQ(obl::game::CurrentStateIndex(game), 5);
}

OBL_TEST_CASE("state_helpers.message_type_count")
{
    OBL_CHECK_EQ(kMessageTypeCount, MT_LEAVE_BUILDING + 1);
}

OBL_TEST_CASE("state_helpers.grid_cells_and_areas")
{
    // Cell lookup wraps both coordinates modulo the dimensions, like the grid's tile-index and tile-object-list lookups.
    Grid grid{};
    grid.m_width = 3;
    grid.m_height = 2;
    grid.m_cells.resize(6);
    auto& cells = grid.m_cells;
    OBL_CHECK_EQ(grid.NumCells(), 6u);
    OBL_CHECK_EQ(grid.TileIndex(2, 1), 5u);
    OBL_CHECK_EQ(grid.TileIndex(3, 2), 0u);
    OBL_CHECK_EQ(grid.TileIndex(4, 3), 4u);
    OBL_CHECK(&grid.Cell(1, 1) == &cells[4]);

    // AreaAt answers null for an index out of range.
    std::vector<obl::map::Area> areas(3);
    OBL_CHECK(Grid::AreaAt(areas, 2) == &areas[2]);
    OBL_CHECK(Grid::AreaAt(areas, 3) == nullptr);
    OBL_CHECK(Grid::AreaAt(areas, -1) == nullptr);
}
