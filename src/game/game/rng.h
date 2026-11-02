#pragma once

#include <cstdint>

namespace obl {

// The simulation's 15-bit stream. Unsigned overflow is part of the saved behavior.
class GameRng {
public:
    explicit GameRng(uint32_t seed = 1) : m_state(seed) {}
    void Seed(uint32_t seed) { m_state = seed; }
    int Draw()
    {
        m_state = m_state * 214013u + 2531011u;
        return static_cast<int>((m_state >> 16) & 0x7fffu);
    }

private:
    uint32_t m_state;
};

namespace game {
using RandSource = int (*)();
int Rand();
// Rand() calls on this thread so far (whatever source answers them); self-checks compare differences.
uint64_t RandCalls();
// Draw directly from the source, bypassing the script tape.
int DrawGameRand();
void SeedGameRand(uint32_t seed);
void SetGameRand(RandSource source);
} // namespace game
} // namespace obl
