#pragma once

#include "game/game/game_core.h"
#include "game/game/progress_file.h"
#include "game/world/scenario_registry.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace obl::scenario_progress {

enum class ScenarioState : uint8_t { Locked, Unlocked, AlwaysAvailable };

// The user's progress per scenario, keyed by ScenarioRef. The one backend is the engine's 12-slot table (the menu slot of the
// registry entry picks the slot), stored as progress.json and seeded from sp.bin; the flag values are game_core.h's. Nothing outside
// this class reads the table, so progress for scenarios outside it can get another backend later. The unlock rules stay in game_core.
class ScenarioProgress {
public:
    using Registry = scenario_load::ScenarioRegistry;
    using Ref = scenario_load::ScenarioRef;

    // Every entry locked, then `initial` unlocked and `always` always available (menu slots).
    void Reset(std::span<const int32_t> initial, std::span<const int32_t> always)
    {
        int32_t selected = 0;
        game_core::Reset(m_slots, selected, initial, always);
    }

    ScenarioState State(const Registry& registry, Ref ref) const
    {
        const int32_t flags = Flags(registry, ref);
        if (flags & 4) return ScenarioState::AlwaysAvailable;
        return (flags & 1) ? ScenarioState::Locked : ScenarioState::Unlocked;
    }
    bool IsComplete(const Registry& registry, Ref ref) const { return (Flags(registry, ref) & 8) != 0; }
    // An unknown ref is not selectable.
    bool IsSelectable(const Registry& registry, Ref ref, bool sandbox) const
    {
        return registry.Find(ref) && game_core::Selectable(Flags(registry, ref), sandbox);
    }

    // The campaign slot a loaded progress selects: the one after the last completed, 0 in the sandbox.
    int32_t SelectAfterLoad(bool sandbox, int32_t campaignCount) const
    {
        return game_core::SelectAfterLoad(m_slots, sandbox, campaignCount);
    }
    // Marks `ref` completed and unlocks its successor in the campaign order.
    void Complete(const Registry& registry, Ref ref, int32_t campaignCount)
    {
        if (const auto* entry = registry.Find(ref)) game_core::Complete(m_slots, entry->menuSlot, campaignCount);
    }
    void UnlockAll(bool sandbox, int32_t campaignCount) { game_core::UnlockAll(m_slots, sandbox, campaignCount); }

    std::string ToJson() const { return progress_file::ToJson(m_slots); }
    // False (progress untouched) for a document or seed file that does not parse.
    bool FromJson(std::string_view text) { return progress_file::FromJson(text, m_slots); }
    bool FromSeedFile(std::string_view bytes) { return progress_file::FromSeedFile(bytes, m_slots); }

private:
    int32_t Flags(const Registry& registry, Ref ref) const
    {
        const auto* entry = registry.Find(ref);
        return entry ? m_slots[entry->menuSlot] : 1;
    }

    int32_t m_slots[game_core::kSlots] = {};
};

} // namespace obl::scenario_progress
