#include "determinism.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/game/game_api.h"
#include "core/log.h"
#include "test_util.h"

#include <cstdlib>
#include <cstdint>
#if !defined(__EMSCRIPTEN__)
#include <immintrin.h>
#endif
#include <cstring>

namespace obl::test {
namespace {

double g_fixedDt;
std::optional<uint32_t> g_lastSeed;
thread_local RandTape g_randTape;
using RandSource = int(__cdecl*)();
RandSource g_randSource;

int __cdecl TapedRand()
{
    return g_randTape.Draw(g_randSource);
}

bool InitRand()
{
    // Only C++ runs, so nothing calls the C runtime rand: every draw is game::Rand().
    g_randSource = &game::DrawGameRand;
    game::SetGameRand(&TapedRand);
    OBL_LOG("script rand: counter/tape enabled on GameRng");
    return true;
}

void TestRandTape()
{
    // Baseline draws bypass the slot; compare the next source value after replay as well as tape values.
    auto seed = game::SeedGameRand;
    seed(12345);
    int expected[5];
    for (int& v : expected) v = g_randSource();
    seed(12345);
    g_randTape.ResetCalls();
    bool ok = g_randTape.BeginRecord();
    for (int i = 0; i < 4; ++i) ok = (game::Rand() == expected[i]) && ok;
    ok = g_randTape.BeginReplay() && ok;
    for (int i = 0; i < 4; ++i) ok = (game::Rand() == expected[i]) && ok;
    ok = g_randTape.End() && ok;
    ok = (g_randTape.Calls() == 4 && game::Rand() == expected[4]) && ok;
    OBL_LOG("autotest: %s rand-test values/count/next-CRT rand-calls=%llu", ok ? "PASS" : "FAILED",
            static_cast<unsigned long long>(g_randTape.Calls()));
}

game::GameClockStep g_gameClockUpdate;

void WrappedGameClockUpdate(game::GameClock* clock, uint32_t tick)
{
    g_gameClockUpdate(clock, tick);
    if (g_fixedDt <= 0) return;
    // time (s), previous time, delta: the world update, GuiManager::Update and Sound::Update read the delta only.
    // Both used to derive from GetTickCount; the frame counter and the FPS bookkeeping stay as they are.
    clock->m_deltaTime = static_cast<float>(g_fixedDt);
    clock->m_time = clock->m_previousTime + static_cast<float>(g_fixedDt);
}

} // namespace

bool InitDeterminism()
{
    return InitRand() && game::WrapGameClockUpdate(&WrappedGameClockUpdate, &g_gameClockUpdate);
}

bool SeedGameRand(unsigned seed)
{
    if (!g_randTape.ResetCalls()) return false;
    ::obl::game::SeedGameRand(seed);
    return true;
}

void SetFixedDt(double seconds)
{
    g_fixedDt = seconds;
}
double FixedDt()
{
    return g_fixedDt;
}
std::optional<uint32_t> LastSeed()
{
    return g_lastSeed;
}

RandTape& GameRandTape()
{
    return g_randTape;
}

void ResetDeterminism()
{
    g_fixedDt = 0;
    g_lastSeed.reset();
}

bool ExecuteDeterminismCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "seed") {
        if (args.size() != 1) {
            OBL_LOG("autotest: FAILED seed (needs one number)");
            return true;
        }
        // Seed the simulation stream on the game thread, before the next draw.
        auto seed = static_cast<unsigned>(std::strtoul(args[0].c_str(), nullptr, 0));
        if (!SeedGameRand(seed)) {
            OBL_LOG("autotest: FAILED seed (RNG tape active)");
            return true;
        }
        g_lastSeed = seed;
        OBL_LOG("autotest: seed seed=%u", seed);
    } else if (name == "rand-test" && args.empty()) {
        TestRandTape();
    } else if (name == "fixed-dt") {
        if (args.size() != 1) {
            OBL_LOG("autotest: FAILED fixed-dt (needs seconds or off)");
            return true;
        }
        g_fixedDt = args[0] == "off" ? 0 : std::atof(args[0].c_str());
        OBL_LOG("autotest: fixed-dt fixed-dt=%g", g_fixedDt);
    } else if (name == "fp-report") {
        if (!args.empty()) {
            OBL_LOG("autotest: FAILED fp-report (takes no arguments)");
            return true;
        }
        // Read the two hardware registers separately: the CRT control API can merge their flags.
        // Ignore MXCSR's sticky exception status bits, which change with ordinary arithmetic.
        // x64 code never uses the legacy FPU and MSVC has no inline asm there: report its reset value.
        const uint16_t x87 = 0x27f;
#if defined(__EMSCRIPTEN__)
        const uint32_t mxcsr = 0x1f80; // wasm has no MXCSR: its float math is always round-to-nearest, masked, no FTZ/DAZ
#else
        const uint32_t mxcsr = _mm_getcsr();
#endif
        OBL_LOG("autotest: fp-report x87-control=%u x87-masks=%u x87-precision=%u x87-rounding=%u "
                "sse-control=%u sse-masks=%u sse-rounding=%u sse-ftz=%u sse-daz=%u",
                static_cast<unsigned>(x87), x87 & 0x3fu, (x87 >> 8) & 3u, (x87 >> 10) & 3u, mxcsr & ~0x3fu, (mxcsr >> 7) & 0x3fu,
                (mxcsr >> 13) & 3u, (mxcsr >> 15) & 1u, (mxcsr >> 6) & 1u);
    } else {
        return false;
    }
    return true;
}

} // namespace obl::test
