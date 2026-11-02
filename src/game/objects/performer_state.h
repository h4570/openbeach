#pragma once

#include <cstdint>

// Performer state support: Singer and Aerobic share HasState logic; Cabaret uses HasBasicState.
// States are the script states of the show: 0 idle, 1 start, 4 and 6 are the stage states; the singer has no state 4.
namespace obl::performer {

inline bool SingerHasState(uint32_t state)
{
    return state <= 1 || state == 6;
}
inline bool ShowHasState(uint32_t state)
{
    return state == 0 || state == 1 || state == 4 || state == 6;
} // Catwalk, WeightLifter, WetTShirt

} // namespace obl::performer
