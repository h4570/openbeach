#include "game/game/rng.h"

namespace obl::game {
namespace {
thread_local GameRng g_rng;
RandSource g_rand = &DrawGameRand;
thread_local uint64_t g_randCalls = 0;
} // namespace

int DrawGameRand()
{
    return g_rng.Draw();
}
int Rand()
{
    ++g_randCalls;
    return g_rand();
}
uint64_t RandCalls()
{
    return g_randCalls;
}
void SeedGameRand(uint32_t seed)
{
    g_rng.Seed(seed);
}
void SetGameRand(RandSource source)
{
    g_rand = source ? source : &DrawGameRand;
}
} // namespace obl::game
