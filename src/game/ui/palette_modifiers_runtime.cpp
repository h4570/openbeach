#include "game/ui/palette_modifiers.h"

#include "game/game/game_accessors.h"
#include "game/ui/color_mix.h"
#include "game/game/diagnostics.h"
#include "game/ui/renderer_types.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "core/pack_path.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace obl::ui {
namespace {

using ui::Renderer;
using ui::Sprite;
using ui::SpriteInstance;
namespace pm = palette_modifiers;

test::CallCounter g_calls("ui.palette-modifiers");
test::CallCounter g_mixCalls("ui.color16-mix");

void* g_currentPaletteModifiers{};

uint16_t* Tables(void* modifiers)
{
    return static_cast<pm::Object*>(modifiers)->tables;
}
uint16_t* Result(void* modifiers)
{
    return static_cast<pm::Object*>(modifiers)->result;
}

// ---- Load / Construct ---------------------------------------------------------------------------------------------------------

[[noreturn]] void Fatal(const std::string& message)
{
    game::FatalError("%s", message.c_str());
    std::abort(); // FatalError exits the process
}

void LoadTables(void* modifiers)
{
    std::string error;
    if (!pm::LoadPack(pack_path::Selected(), game::GetRenderer()->m_rgb555 == 0, Tables(modifiers), error))
        Fatal("Palette modifiers: " + error + ". Import the game data again");
}

void* InitializeTables(void* modifiers)
{
    SetPaletteModifiers(modifiers);
    LoadTables(modifiers);
    return modifiers;
}

// ---- BuildTonedPalette[Ex] ----------------------------------------------------------------------------------------------------

color_mix::PixelFormat CurrentFormat()
{
    return {game::GetPixelBlueMask(), game::GetPixelGreenMask(), game::GetPixelRedMask(), static_cast<uint8_t>(game::GetPixelGreenShift()),
            static_cast<uint8_t>(game::GetPixelRedShift()), game::GetPixelGreenMax()};
}

uint32_t Mix(uint32_t first, uint32_t second, float weight)
{
    g_mixCalls.Count();
    return color_mix::Mix(CurrentFormat(), first, second, weight);
}

// The sprite palette copied to the result, then the planned entries mixed over it. Null when the sprite has no modifier id.
const uint16_t* TonePalette(void* modifiers, const SpriteInstance* instance, float scale, float blend, bool extra)
{
    const Sprite* sprite = instance->m_sprite;
    const pm::Plan plan = pm::MakePlan(sprite->m_paletteModifierId, instance->m_paletteIndex / 10, scale, blend, extra);
    if (!plan.valid) return nullptr;
    uint16_t* result = Result(modifiers);
    std::memcpy(result, sprite->m_palettes[instance->m_paletteIndex], pm::kTableEntries * sizeof(uint16_t));
    pm::Apply(plan, Tables(modifiers), result, Mix);
    return result;
}

// ---- drivers ------------------------------------------------------------------------------------------------------------------

struct Rng {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int Range(int low, int high) { return low + static_cast<int>(Next() % static_cast<uint32_t>(high - low + 1)); }
    bool Chance(int percent) { return static_cast<int>(Next() % 100) < percent; }
};

// BuildTonedPalette and ...Ex with synthetic sprites: every modifier id (including 0 and out-of-range), 100 palettes, scale / blend values around the
// epsilon and the thresholds, the Ex flag with stray high bytes, and (half of the cases) random tables instead of the file's.
void DriveBuild(int count)
{
    void* modifiers = CurrentPaletteModifiers();
    if (!modifiers || !game::GetRenderer()) return;
    std::vector<uint8_t> saved(pm::kObjectBytes);
    std::memcpy(saved.data(), modifiers, saved.size());

    std::vector<std::vector<uint16_t>> palettes(100, std::vector<uint16_t>(pm::kTableEntries));
    std::vector<uint16_t*> pointers(100);
    Sprite sprite{};
    SpriteInstance instance{};
    sprite.m_palettes = pointers.data();
    sprite.m_paletteCount = 100;
    instance.m_sprite = &sprite;

    constexpr float kEpsilon = 1.1920929e-07f;
    const float scales[] = {0.0f, 0.25f, 0.5f, 0.999f, 1.0f, 1.0f + kEpsilon, 1.0f + 2 * kEpsilon, 1.0001f, 1.3f, 1.5f, 2.0f, 10.0f, -1.0f,
            1e-30f, std::numeric_limits<float>::quiet_NaN()};
    const float blends[] = {0.0f, 1e-8f, kEpsilon, 2 * kEpsilon, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, -0.5f};
    const uint32_t extras[] = {0, 1, 0xff, 0x100, 0x1234, 0x80000000u, 0xffffff00u};

    Rng rng{23};
    for (int i = 0; i < count; ++i) {
        if (rng.Chance(50)) {
            auto* words = reinterpret_cast<uint16_t*>(modifiers);
            for (size_t w = 0; w < pm::kObjectBytes / 2; ++w) words[w] = static_cast<uint16_t>(rng.Next());
        } else {
            std::memcpy(modifiers, saved.data(), saved.size());
        }
        for (size_t p = 0; p < palettes.size(); ++p) {
            for (auto& entry : palettes[p]) entry = static_cast<uint16_t>(rng.Next());
            pointers[p] = palettes[p].data();
        }
        sprite.m_paletteModifierId = rng.Chance(70) ? rng.Range(1, 4) : rng.Range(-2, 6);
        instance.m_paletteIndex = rng.Range(0, 99);
        const float scale = rng.Chance(70) ? scales[rng.Range(0, 14)] : static_cast<float>(rng.Range(0, 3000)) / 1000.0f;
        const float blend = rng.Chance(60) ? 0.0f
                : rng.Chance(70)           ? blends[rng.Range(0, 10)]
                                           : static_cast<float>(rng.Range(0, 1000)) / 1000.0f;
        const uint32_t extra = extras[rng.Range(0, 6)];
        if (i % 2 == 0)
            BuildTonedPalette(modifiers, &instance, scale, blend);
        else
            BuildTonedPaletteWithExtra(modifiers, &instance, scale, blend, extra);
    }
    std::memcpy(modifiers, saved.data(), saved.size());
    OBL_LOG("autotest: palette-modifiers-build-drive cases=%d", count);
}

} // namespace

void* CurrentPaletteModifiers()
{
    return g_currentPaletteModifiers;
}

void SetPaletteModifiers(void* modifiers)
{
    g_currentPaletteModifiers = modifiers;
}

void* ConstructPaletteModifiers(void* modifiers)
{
    return InitializeTables(modifiers);
}

const uint16_t* BuildTonedPalette(void* modifiers, SpriteInstance* instance, float scale, float blend)
{
    g_calls.Count();
    return TonePalette(modifiers, instance, scale, blend, false);
}

const uint16_t* BuildTonedPaletteWithExtra(void* modifiers, SpriteInstance* instance, float scale, float blend, uint32_t extra)
{
    g_calls.Count();
    return TonePalette(modifiers, instance, scale, blend, (extra & 0xff) != 0);
}

bool RegisterPaletteDrivers()
{
    g_calls.SetDriver(&DriveBuild);
    return true;
}

} // namespace obl::ui
