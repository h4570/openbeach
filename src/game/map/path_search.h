#pragma once

#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/map/path_state.h"

#include <bit>
#include <vector>

namespace obl::path {

inline int32_t Signed(uint32_t value)
{
    return std::bit_cast<int32_t>(value);
}

// Sizes the node array to the grid on the first search (fresh nodes start at zero, as the constructed cells did) and gives each node its
// tile; later searches find it sized and leave the stale bookkeeping alone.
inline void PrepareNodes(PathSearch& search)
{
    const map::Grid& grid = *search.m_grid;
    if (search.m_nodes.size() == grid.NumCells()) return;
    search.m_nodes.assign(grid.NumCells(), Node{});
    for (uint32_t i = 0; i < grid.NumCells(); ++i) {
        search.m_nodes[i].m_tileX = i % grid.m_width;
        search.m_nodes[i].m_tileY = i / grid.m_width;
    }
}

inline Node& NodeAt(PathSearch& search, uint32_t x, uint32_t y)
{
    return search.m_nodes[search.m_grid->TileIndex(x, y)];
}

inline void ClearOpenList(PathSearch& search)
{
    auto& head = search.m_openHead;
    head.m_parent = head.m_nextOpen = nullptr;
    head.m_search[0] = 0;
    head.m_search[1] = search.m_search;
    for (auto& successor : head.m_successors) successor = nullptr;
    head.m_closed = head.m_open = 0;
    head.m_f = head.m_h = head.m_g = 0;
    head.m_tileX = head.m_tileY = 0xffffffff;
    search.m_endTile = nullptr;
}

inline void InsertOpenTile(PathSearch& search, Node& tile)
{
    auto& head = search.m_openHead;
    const uint32_t serial = search.m_search;
    if (!head.m_nextOpen || head.m_search[1] != serial) {
        // These early returns deliberately leave the inserted tile's stale link alone.
        head.m_nextOpen = &tile;
        head.m_search[1] = serial;
        return;
    }
    if (head.m_nextOpen->m_search[0] != serial) {
        head.m_search[1] = serial;
        head.m_nextOpen = &tile;
        tile.m_search[0] = serial;
        return;
    }
    Node* previous = &head;
    Node* current = head.m_nextOpen;
    while (current && previous->m_search[1] == serial) {
        if (current->m_search[0] != serial || Signed(tile.m_f) <= Signed(current->m_f)) break;
        previous = current;
        if (!current->m_nextOpen || current->m_search[1] != serial) {
            current = nullptr;
            break;
        }
        current = current->m_nextOpen;
    }
    if (previous->m_search[1] != serial) current = nullptr;
    tile.m_search[1] = serial;
    tile.m_nextOpen = current;
    previous->m_nextOpen = &tile;
    previous->m_search[1] = previous->m_search[0] = serial;
}

inline void AddSuccessor(const PathSearch& search, Node& tile, Node& successor)
{
    for (auto& slot : tile.m_successors)
        if (!slot || slot->m_search[0] != search.m_search) {
            slot = &successor;
            break;
        }
}

inline void PropagateLowerCost(PathSearch& search, Node& tile)
{
    std::vector<Node*> pending;
    auto relax = [&](Node& parent) {
        for (auto* child : parent.m_successors) {
            if (!child || child->m_search[0] != search.m_search) break;
            const uint32_t cost = parent.m_g + 1;
            if (Signed(cost) < Signed(child->m_g)) {
                child->m_g = cost;
                child->m_f = cost + child->m_h;
                child->m_parent = &parent;
                pending.push_back(child);
            }
        }
    };
    relax(tile);
    while (!pending.empty()) {
        auto* parent = pending.back();
        pending.pop_back();
        relax(*parent);
    }
}

inline uint32_t Heuristic(const PathSearch& search, uint32_t x, uint32_t y)
{
    const auto p = grid_math::TileToWorldPoint(search.m_grid->m_tileSize, x, y, 4);
    const auto dx = static_cast<uint32_t>(static_cast<int32_t>(static_cast<double>(p.x) - search.m_startWorld[0]));
    const auto dy = static_cast<uint32_t>(static_cast<int32_t>(static_cast<double>(p.y) - search.m_startWorld[1]));
    return dx * dx + dy * dy;
}

inline void RelaxNeighbour(PathSearch& search, Node& parent, uint32_t x, uint32_t y, uint32_t moveCost)
{
    auto& child = NodeAt(search, x, y);
    const uint32_t cost = parent.m_g + moveCost;
    if ((child.m_open || child.m_closed) && child.m_search[0] == search.m_search) {
        AddSuccessor(search, parent, child);
        if (Signed(cost) < Signed(child.m_g)) {
            child.m_parent = &parent;
            child.m_g = cost;
            child.m_f = cost + child.m_h;
            // Open wins over closed (popped cells retain their open byte). Never reorder the list here.
            if (!child.m_open) PropagateLowerCost(search, child);
        }
        return;
    }
    child.m_parent = &parent;
    child.m_g = cost;
    child.m_h = Heuristic(search, x, y);
    child.m_f = cost + child.m_h;
    child.m_open = 1;
    child.m_search[0] = search.m_search;
    InsertOpenTile(search, child);
    AddSuccessor(search, parent, child);
}

template<class Allowed> void ExpandNeighbours(PathSearch& search, Node& tile, Allowed&& allowed)
{
    grid_math::TileXY neighbours[8];
    for (int d = 0; d < 8; ++d)
        neighbours[d] = grid_math::Neighbour(search.m_grid->m_width, search.m_grid->m_height, tile.m_tileX, tile.m_tileY, d);
    for (int i = 0; i < 8; ++i) {
        const auto n = neighbours[(i + 7) % 8];
        if (allowed(n.x, n.y)) RelaxNeighbour(search, tile, n.x, n.y, i % 2 ? 0x800 : 0x1000);
    }
}

template<class Allowed> bool Search(PathSearch& search, uint32_t startX, uint32_t startY, uint32_t stopX, uint32_t stopY, Allowed&& allowed)
{
    search.m_startX = startX;
    search.m_startY = startY;
    auto setPoint = [&](float* out, uint32_t x, uint32_t y) {
        auto p = grid_math::TileToWorldPoint(search.m_grid->m_tileSize, x, y, 4);
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
    };
    setPoint(search.m_stopWorld, stopX, stopY);
    setPoint(search.m_startWorld, startX, startY);
    PrepareNodes(search);
    auto& stop = NodeAt(search, stopX, stopY);
    stop.m_g = stop.m_h = stop.m_f = 0;
    stop.m_tileX = stopX;
    stop.m_tileY = stopY;
    stop.m_search[0] = stop.m_search[1] = search.m_search;
    stop.m_open = 1;
    stop.m_closed = 0;
    stop.m_nextOpen = stop.m_parent = nullptr;
    search.m_endTile = nullptr;
    search.m_openHead.m_nextOpen = &stop;
    search.m_openHead.m_search[1] = search.m_search;
    while (search.m_openHead.m_nextOpen && search.m_openHead.m_search[1] == search.m_search) {
        auto* tile = search.m_openHead.m_nextOpen;
        search.m_openHead.m_nextOpen = tile->m_nextOpen && tile->m_search[1] == search.m_search ? tile->m_nextOpen : nullptr;
        search.m_openHead.m_search[1] = search.m_search;
        tile->m_closed = 1;
        tile->m_search[0] = search.m_search;
        if (tile == &NodeAt(search, startX, startY)) {
            search.m_endTile = tile;
            return true;
        }
        ExpandNeighbours(search, *tile, allowed);
    }
    return false;
}
} // namespace obl::path
