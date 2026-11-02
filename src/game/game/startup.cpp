// Process start-up: the tables and lookup tables the game reads, built once in a fixed order before the first frame.

#include "game/game/startup.h"
#include "game/game/startup_tables.h"
#include "game/map/water_anim.h"
#include "game/objects/fast_sqrt_table.h"
#include "game/ui/renderer_types.h"
#include "core/call_counter.h"
#include "core/log.h"
#include "core/platform_main.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace obl::game {

namespace {

uint32_t g_fastSqrtTable[65536]{};
float g_fastSinTable[platform_main::kFastSinTableSize]{};

uint32_t g_startupCalls{};
bool g_snapshotTaken{};
uint32_t g_hashAll{}, g_hashSqrt{}, g_hashSin{};

uint32_t Fnv1a(uint32_t hash, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

test::CallCounter g_startupReport("StaticInit");
void Report(int)
{
    OBL_LOG("autotest: static-init cpp-init=%u snapshot=%d hash=%u sqrt-hash=%u sin-hash=%u", g_startupCalls, g_snapshotTaken ? 1 : 0,
            g_hashAll, g_hashSqrt, g_hashSin);
}

} // namespace

void RunStartup(RegisterExitHandler onexit)
{
    ++g_startupCalls;
    InitStartupTables();
    ui::ConstructWaterBlockImage();
    onexit(&ui::DestroyWaterBlockImage);
    fast_sqrt::BuildTable(g_fastSqrtTable);
    platform_main::BuildFastSinTable(g_fastSinTable);
}

void SnapshotStartupData()
{
    if (g_snapshotTaken) return;
    g_snapshotTaken = true;
    constexpr uintptr_t kWaterBlockImageSize = offsetof(ui::RendererImage, m_pixels); // pointers differ between runs
    // The startup hash is a recorded reference over this exact block: the three gradient steps at fixed places, zero elsewhere.
    const ui::RendererLoadingGradient& gradient = LoadingGradient();
    uint8_t gradientBlock[0x48]{};
    std::memcpy(gradientBlock + 0x18, &gradient.m_blueStep, sizeof(float));
    std::memcpy(gradientBlock + 0x2c, &gradient.m_redStep, sizeof(float));
    std::memcpy(gradientBlock + 0x44, &gradient.m_greenStep, sizeof(float));
    uint32_t hash = 2166136261u;
    hash = Fnv1a(hash, gradientBlock, sizeof(gradientBlock));
    hash = Fnv1a(hash, &ui::WaterBlockImage(), kWaterBlockImageSize);
    hash = Fnv1a(hash, GameSpeedTable(), 3 * sizeof(float));
    hash = Fnv1a(hash, PositionOffset(1), 2 * sizeof(float));
    hash = Fnv1a(hash, PositionOffset(2), 2 * sizeof(float));
    // Hash the pointed-to bytes: literals of different builds have different addresses.
    for (const char* const* names : {GuiListSpriteNames(), GuiMainItemSpriteNames()})
        for (int i = 0; i < 3; ++i) {
            const char* name = names[i];
            hash = Fnv1a(hash, name, std::strlen(name) + 1);
        }
    g_hashSqrt = Fnv1a(2166136261u, g_fastSqrtTable, sizeof(g_fastSqrtTable));
    g_hashSin = Fnv1a(2166136261u, g_fastSinTable, sizeof(g_fastSinTable));
    g_hashAll = hash ^ g_hashSqrt ^ g_hashSin;
}

void RegisterStartupReport()
{
    g_startupReport.SetDriver(Report);
}

} // namespace obl::game
