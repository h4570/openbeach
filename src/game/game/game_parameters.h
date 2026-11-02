#pragma once

#include <cstdint>

// The game parameter list, populated from rules/game_parameters.json.
namespace obl::game {

struct GameParameterNode {
    char m_name[0x100]; // key as written in the script, type prefix included (`bFLAG`, `iVALUE`, `fRATE`, `sTEXT`)
    uint8_t m_boolValue; // `b` entries: the script said "true"
    int m_intValue; // `i` entries: the script float truncated toward zero
    uint32_t m_floatBits; // `f` entries: the script float, bit pattern (no accessor reads it as a float)
    char* m_string; // `s` entries: 0x100-byte game-heap copy, otherwise null
    GameParameterNode* m_next;
};

struct GameParameters {
    GameParameterNode* m_first;
};

} // namespace obl::game
