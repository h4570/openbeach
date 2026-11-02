#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace obl::game_core {

// The engine's progress table has 12 slots; a campaign with fewer entries (`count`) leaves the rest at 1 (locked, never selectable).
inline constexpr int32_t kSlots = 12;

inline void Reset(int32_t (&progress)[12], int32_t& selected)
{
    std::fill_n(progress, 12, 1);
    progress[0] = progress[1] = 4;
    progress[2] = 2;
    selected = 0;
}

// The same reset from the campaign's start availability (scenario_load::Campaign, menu indices): every entry locked, the ones in `initial`
// unlocked, the ones in `always` always available. Applied in that order, so an always-available entry keeps its 4 (the built-in Reset
// above is this call with s1, s2 always and s3 initially).
inline void Reset(int32_t (&progress)[12], int32_t& selected, std::span<const int32_t> initial, std::span<const int32_t> always)
{
    std::fill_n(progress, 12, 1);
    for (const int32_t index : initial) progress[index] = 2;
    for (const int32_t index : always) progress[index] = 4;
    selected = 0;
}

inline int32_t SelectAfterLoad(const int32_t (&progress)[12], bool sandbox, int32_t count = kSlots)
{
    if (sandbox) return 0;
    int32_t completed = -1;
    for (int32_t i = 0; i < count; ++i)
        if (progress[i] & 8) completed = i;
    return std::min(completed + 1, count - 1);
}

inline void Complete(int32_t (&progress)[12], int32_t selected, int32_t count = kSlots)
{
    progress[selected] |= 8;
    const uint32_t next = static_cast<uint32_t>(selected) + 1;
    if (next < static_cast<uint32_t>(count) && !(progress[next] & 12)) progress[next] = 2;
}

// The Shift+U+Alt cheat of the game update outside a world: campaign unlocks every scenario (the first two always available),
// sandbox marks all of them completed. Slots past `count` stay locked.
inline void UnlockAll(int32_t (&progress)[12], bool sandbox, int32_t count = kSlots)
{
    std::fill_n(progress, 12, 1);
    std::fill_n(progress, count, sandbox ? 8 : 2);
    if (!sandbox) progress[0] = progress[count > 1 ? 1 : 0] = 4;
}

// The names the scenario scan lists for a campaign of `campaign` scenarios and `sandboxes` sandboxes: the installed directory names
// (`Scenario<n>`, `SandScenario<n>`) in the file system's name order, so the list needs no installed Data/Scenario tree.
inline std::vector<std::string> ScenarioListNames(int32_t campaign, int32_t sandboxes)
{
    std::vector<std::string> names;
    for (int32_t n = 1; n <= campaign; ++n) names.push_back("Scenario" + std::to_string(n));
    for (int32_t n = 1; n <= sandboxes; ++n) names.push_back("SandScenario" + std::to_string(n));
    std::sort(names.begin(), names.end());
    return names;
}

inline bool Selectable(int32_t progress, bool sandbox)
{
    return sandbox ? (progress & 12) != 0 : (progress & 1) == 0;
}

} // namespace obl::game_core
