#include "core/char16.h"
#include "unit_test.h"
#include "game/game/rng.h"
#include "core/rand_tape.h"

#include <array>

#ifdef _WIN32
#include <windows.h>
#endif

OBL_TEST_CASE("game_rng.fixed_sequences_and_reseed")
{
    constexpr std::array kSeeds{1u, 0u, 0xffffffffu};
    constexpr int kExpected[][10] = {
            {41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464},
            {38, 7719, 21238, 2437, 8855, 11797, 8365, 32285, 10450, 30612},
            {35, 29739, 3374, 11141, 31308, 7870, 5253, 2445, 26706, 3992},
    };
    obl::GameRng rng;
    OBL_CHECK_EQ(rng.Draw(), 41);
    for (size_t i = 0; i < kSeeds.size(); ++i) {
        rng.Seed(kSeeds[i]);
        for (int value : kExpected[i]) OBL_CHECK_EQ(rng.Draw(), value);
    }
    rng.Seed(1);
    OBL_CHECK_EQ(rng.Draw(), 41);
}

OBL_TEST_CASE("game_rng.streams_are_independent_and_wrap_at_32_bits")
{
    obl::GameRng a(0xffffffffu), b(0xffffffffu);
    // 64-bit arithmetic is a separate oracle for the modulo-2^32 contract.
    uint64_t state = 0xffffffffu;
    for (int i = 0; i < 100000; ++i) {
        state = (state * 214013 + 2531011) % (uint64_t{1} << 32);
        const int expected = static_cast<int>((state / 65536) % 32768);
        OBL_CHECK_EQ(a.Draw(), expected);
        OBL_CHECK_EQ(b.Draw(), expected);
    }
    a.Seed(0);
    OBL_CHECK_EQ(a.Draw(), 38);
    state = (state * 214013 + 2531011) % (uint64_t{1} << 32);
    OBL_CHECK_EQ(b.Draw(), static_cast<int>((state / 65536) % 32768));
}

OBL_TEST_CASE("game_rng.tape_replay_does_not_advance_plain_source")
{
    obl::RandTape tape;
    obl::game::SeedGameRand(1);
    OBL_CHECK(tape.BeginRecord());
    OBL_CHECK_EQ(tape.Draw(obl::game::DrawGameRand), 41);
    OBL_CHECK_EQ(tape.Draw(obl::game::DrawGameRand), 18467);
    OBL_CHECK(tape.BeginReplay());
    OBL_CHECK_EQ(tape.Draw(obl::game::DrawGameRand), 41);
    OBL_CHECK_EQ(tape.Draw(obl::game::DrawGameRand), 18467);
    OBL_CHECK(tape.End());
    OBL_CHECK_EQ(tape.Calls(), uint64_t{2});
    OBL_CHECK_EQ(obl::game::Rand(), 6334);
    obl::game::SeedGameRand(1);
}

#ifdef _WIN32
OBL_TEST_CASE("game_rng.matches_msvcrt_source")
{
    HMODULE module = LoadLibraryW(L"msvcrt.dll");
    OBL_REQUIRE(module != nullptr);
    const auto seed = reinterpret_cast<void(__cdecl*)(unsigned)>(GetProcAddress(module, "srand"));
    const auto draw = reinterpret_cast<int(__cdecl*)()>(GetProcAddress(module, "rand"));
    OBL_REQUIRE(seed != nullptr && draw != nullptr);
    for (uint32_t initial : {0u, 1u, 12345u, 0x80000000u, 0xffffffffu}) {
        seed(initial);
        obl::GameRng rng(initial);
        for (int i = 0; i < 100000; ++i) OBL_CHECK_EQ(rng.Draw(), draw());
    }
    seed(1);
    FreeLibrary(module);
}
#endif
