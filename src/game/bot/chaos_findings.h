#pragma once

#include "game/ui/sprite.h"
#include "game/map/grid_math.h"
#include "game/map/cell_objects.h"
#include "game/map/grid_state.h"
#include "game/objects/object_building.h"
#include "game/construction/placement_types.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace obl::chaos {

// The daily counters are not a transaction log. Compare sampled same-day deltas and
// expose booked refunds separately; scenery deliberately books a different refund from cash.
class CashLedger {
public:
    std::vector<std::string> Observe(const layout::World& world)
    {
        if (!world.m_player) {
            m_previous.reset();
            return {};
        }
        const Sample now{world.m_gameDay, world.m_player->m_cash, world.m_incomeToday, world.m_demolishIncomeToday, world.m_costsToday,
                world.m_constructCostToday, world.m_sceneryCostToday};
        const auto previous = m_previous;
        m_previous = now;
        if (!previous) return {};
        if (previous->day != now.day) return {"cash-ledger-reset day=" + std::to_string(now.day)};
        const double cash = now.cash - previous->cash;
        const double income = now.income - previous->income;
        const double refunds = now.refunds - previous->refunds;
        const double costs = now.costs - previous->costs;
        const double residual = cash - (income + refunds - costs);
        // Float cash stores lose small fractions at large balances. This is a diagnostic,
        // not an accounting proof; allow one currency unit plus eight float ULPs of scale.
        const double tolerance = 1.0 + std::max(std::abs(now.cash), std::abs(previous->cash)) * 0x1p-20;
        if (std::isfinite(residual) && std::abs(residual) <= tolerance) return {};
        return {"cash-ledger cash=" + std::to_string(cash) + " income=" + std::to_string(income) + " refunds=" + std::to_string(refunds) +
                " costs=" + std::to_string(costs) + " construction-costs=" + std::to_string(now.construction - previous->construction) +
                " scenery-costs=" + std::to_string(now.scenery - previous->scenery) + " residual=" + std::to_string(residual) +
                " tolerance=" + std::to_string(tolerance)};
    }

private:
    struct Sample {
        int day;
        double cash, income, refunds, costs, construction, scenery;
    };
    std::optional<Sample> m_previous;
};

// Read-only observations, deliberately separate from the hard chaos oracle and world hashes.
inline std::vector<std::string> SoftFindings(const layout::World& world)
{
    std::vector<std::string> findings;
    auto counter = [&](const std::string& name, int64_t value) {
        if (value < 0) findings.push_back("negative-counter " + name + "=" + std::to_string(value));
    };
    counter("world.guests", world.m_numGuests);
    counter("world.buildings", world.m_numBuildings);
    const auto* objects = world.m_objectManager.get();
    const auto* grid = world.m_grid.get();
    if (!objects || !grid || !grid->m_width || !grid->m_height) return findings;
    auto count = [&](const char* name, uint32_t value, uint32_t capacity) {
        if (value > capacity) findings.push_back(std::string("invalid-count ") + name + "=" + std::to_string(value));
    };
    count("objects.buildings", static_cast<uint32_t>(objects->m_buildings.size()), object::Manager::kMaxBuildings);
    count("objects.guests", static_cast<uint32_t>(objects->m_guests.size()), object::Manager::kMaxGuests);
    count("objects.others", static_cast<uint32_t>(objects->m_others.size()), object::Manager::kMaxOthers);
    std::set<const object::Base*> onGrid;
    uint32_t unlisted = 0;
    for (const auto& cell : grid->m_cells)
        for (const map::ObjectId id : cell.m_objects) {
            if (const object::Base* object = cell_objects::ObjectOf(*objects, id))
                onGrid.insert(object);
            else
                ++unlisted;
        }
    if (unlisted) findings.push_back("unlisted-grid-object references=" + std::to_string(unlisted));
    std::vector<bool> footprint(grid->m_cells.size(), false);
    const grid_math::GridDims dims{grid->m_tileSize, grid->m_width, grid->m_height};
    for (size_t i = 0; i < std::min<size_t>(objects->m_buildings.size(), object::Manager::kMaxBuildings); ++i) {
        const auto* base = objects->m_buildings[i];
        if (!base || base->m_type != object::ObjectType::Building) continue;
        const auto& b = *static_cast<const object::Building*>(base);
        const std::string name = "building[" + std::to_string(b.m_id) + "].";
        counter(name + "guests", b.m_NumGuests);
        counter(name + "male-staff", b.m_numMalestaff);
        counter(name + "female-staff", b.m_numFemalestaff);
        counter(name + "nonserve-staff", b.m_NumNonServestaff);
        counter(name + "lilos", b.m_NumLilos);
        counter(name + "boards", b.m_NumBoards);
        counter(name + "snorkels", b.m_NumSnorkels);
        counter(name + "capacity", b.m_CurCapacity);
        if (!onGrid.contains(base)) findings.push_back("building-without-tiles " + name);
        const auto* def = b.m_definition;
        const auto* sprite = static_cast<const ui::SpriteInstance*>(b.m_sprite);
        if (!def || !sprite) continue;
        if (!def->m_footprint) {
            auto origin = grid_math::WorldPointToTile(dims.tileSize, dims.width, dims.height, b.m_WorldXYZ[0], b.m_WorldXYZ[1]);
            const bool turned = (sprite->m_direction & 1) != 0;
            const int rows = turned ? def->m_GridHeight : def->m_GridWidth;
            const int columns = turned ? def->m_GridWidth : def->m_GridHeight;
            if (rows < 0 || rows > 25 || columns < 0 || columns > 25) continue;
            uint32_t missing = 0;
            for (int row = 0; row < rows; ++row) {
                auto tile = origin;
                for (int column = 0; column < columns; ++column) {
                    footprint[grid->TileIndex(tile.x, tile.y)] = true;
                    if (!(grid->Cell(tile.x, tile.y).m_flags & 0x10)) ++missing;
                    tile = grid_math::Neighbour(dims.width, dims.height, tile.x, tile.y, 0);
                }
                origin = grid_math::Neighbour(dims.width, dims.height, origin.x, origin.y, 6);
            }
            if (missing) findings.push_back("missing-footprint " + name + "tiles=" + std::to_string(missing));
            continue;
        }
        const auto* mask = def->m_footprint.get();
        const bool turned = (sprite->m_direction & 1) != 0;
        const int columns = (turned ? def->m_GridHeight : def->m_GridWidth) + 2;
        const int rows = (turned ? def->m_GridWidth : def->m_GridHeight) + 2;
        if (columns < 0 || columns > 25 || rows < 0 || rows > 25) continue;
        auto origin = grid_math::WorldPointToTile(dims.tileSize, dims.width, dims.height, b.m_WorldXYZ[0], b.m_WorldXYZ[1]);
        bool touched = false;
        const auto directions = grid_math::OrientFootprintTraversal(dims, dims, origin, rows, columns, sprite->m_direction, touched);
        // Invalid rotations use the same zero direction defaults as the construction adapter.
        uint32_t missing = 0;
        for (int row = 0; row < rows; ++row) {
            auto tile = origin;
            for (int column = 0; column < columns; ++column) {
                const int r = turned ? column : row, c = turned ? row : column;
                if ((mask->m_layers[0][r][c] != 0 && mask->m_layers[0][r][c] != 0x200) || mask->m_layers[3][r][c] == 1) {
                    footprint[grid->TileIndex(tile.x, tile.y)] = true;
                    if (!(grid->Cell(tile.x, tile.y).m_flags & 0x10)) ++missing;
                }
                tile = grid_math::Neighbour(dims.width, dims.height, tile.x, tile.y, directions.columnDirection);
            }
            origin = grid_math::Neighbour(dims.width, dims.height, origin.x, origin.y, directions.rowDirection);
        }
        if (missing) findings.push_back("missing-footprint " + name + "tiles=" + std::to_string(missing));
    }
    uint32_t orphan = 0;
    for (size_t i = 0; i < footprint.size(); ++i)
        if ((grid->m_cells[i].m_flags & 0x10) && !footprint[i]) ++orphan;
    if (orphan) findings.push_back("orphan-footprint tiles=" + std::to_string(orphan));
    return findings;
}

} // namespace obl::chaos
