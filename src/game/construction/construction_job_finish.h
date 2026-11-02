#pragma once

#include "construction_queries.h"

#include <cstdint>
#include <cstring>

namespace obl::construction {

struct FinishState {
    float cash = 0, income = 0, demolitionIncome = 0, placed = 0;
    int hoardings = 0;
    uint8_t death = 0;
};

// SpawnHoarding's scenery request for the place command. The game side copies it into the record
// (src/game/construction/construction_finish.cpp), so the offsets live only in the layout.
struct HoardingPlacement {
    char m_name[32];
    char m_sprite[32];
    uint32_t m_x;
    uint32_t m_y;
    uint8_t m_usesTilePosition;
    uint8_t m_direction;
    int m_owner;
};

// Env supplies the building's tile, footprint, refund and time, and delegates
// grid updates, job deletion, builder allocation and scenery placement to the game.
// State is published before the first delegated call that can observe its writes.
template<class Env> void FinishBuild(int id, FinishState& state, Env& env)
{
    const auto tile = env.TilePosition();
    env.ApplyFootprint(tile[0], tile[1], 1, 2, id);
    env.Publish(state);
    env.DeleteJob(id);
    env.Reallocate();
}

template<class Env> void FinishDemolish(int id, FinishState& state, Env& env)
{
    env.ClearFootprint(id);
    const auto tile = env.TilePosition();
    env.ApplyFootprint(tile[0], tile[1], 1, 3, id);
    // Asset refunds belong only to demolition statistics, not scenario operating income.
    // The arithmetic rounds only when each result is stored as float.
    const int refund = env.Refund();
    state.cash = static_cast<float>(static_cast<double>(state.cash) + refund);
    const double unsignedRefund = static_cast<uint32_t>(refund);
    state.demolitionIncome = static_cast<float>(static_cast<double>(state.demolitionIncome) + unsignedRefund);
    state.death = 1;
    env.Publish(state);
    env.DeleteJob(id);
    env.Reallocate();
}

inline HoardingPlacement Placement(int id, const construction::Hoarding& hoarding)
{
    HoardingPlacement result{};
    std::strcpy(result.m_name, "singlecell");
    std::strcpy(result.m_sprite, "HOARDING_");
    // _itoa(id, buffer, 20) treats nondecimal negative numbers as unsigned.
    constexpr char kDigits[] = "0123456789abcdefghij";
    char reversed[8];
    int count = 0;
    uint32_t value = static_cast<uint32_t>(id);
    do {
        reversed[count++] = kDigits[value % 20];
        value /= 20;
    } while (value);
    for (int i = 0; i < count; ++i) result.m_sprite[9 + i] = reversed[count - i - 1];
    result.m_x = static_cast<uint32_t>(hoarding.x);
    result.m_y = static_cast<uint32_t>(hoarding.y);
    result.m_usesTilePosition = 1;
    result.m_direction = static_cast<uint8_t>(hoarding.m_direction);
    result.m_owner = -2;
    return result;
}

template<class Env> void SpawnHoarding(int id, FinishState& state, Env& env)
{
    if (!env.HasFootprint() || !env.HasJob() || state.hoardings == 0 ||
            !(static_cast<double>(env.Time()) - static_cast<double>(state.placed) > static_cast<double>(0.1f)))
        return;
    const construction::Hoarding hoarding = env.HoardingAt(--state.hoardings);
    env.Publish(state);
    // Occupied terrain consumes the entry without advancing the placement time.
    if ((env.TileFlags(static_cast<uint32_t>(hoarding.x), static_cast<uint32_t>(hoarding.y)) & 3) != 0) return;
    const auto placement = Placement(id, hoarding);
    env.PostPlace(placement);
    state.placed = env.Time();
    env.Publish(state);
}

} // namespace obl::construction
