#pragma once

#include <cstdint>

namespace obl::path {

struct Node {
    Node* m_parent = nullptr; // predecessor on the route found so far
    Node* m_nextOpen = nullptr; // next node of the open list (ordered by f)
    uint32_t m_search[2] = {}; // numbers of the search that touched the node
    Node* m_successors[8] = {}; // up to eight successors
    uint8_t m_closed = 0;
    uint8_t m_open = 0;
    uint32_t m_f = 0; // f/h/g of the search
    uint32_t m_h = 0;
    uint32_t m_g = 0;
    uint32_t m_tileX = 0; // column / row of the tile (the stop node gets the coordinates the search was given)
    uint32_t m_tileY = 0;
};

} // namespace obl::path
