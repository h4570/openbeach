#pragma once

#include "game/game/game_core.h"
#include "game/game/progress_file.h"
#include "game/world/scenario_registry.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace obl::scenario_progress {

enum class ScenarioState : uint8_t { Locked, Unlocked, AlwaysAvailable };

// The user's progress per scenario, keyed by ScenarioRef. Pack entries use the 12-slot table (menuSlot >= 0); extra entries from
// mod scenario sets use `m_extra` keyed by scenario id, stored in progress.json's optional `"extra"` object.
class ScenarioProgress {
public:
    using Registry = scenario_load::ScenarioRegistry;
    using Ref = scenario_load::ScenarioRef;

    // Every entry locked, then `initial` unlocked and `always` always available (menu slots). Also clears extra entries.
    void Reset(std::span<const int32_t> initial, std::span<const int32_t> always)
    {
        int32_t selected = 0;
        game_core::Reset(m_slots, selected, initial, always);
        m_extra.clear();
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
        const auto* entry = registry.Find(ref);
        if (!entry) return false;
        if (!entry->IsExtra()) return game_core::Selectable(m_slots[entry->menuSlot], sandbox);
        return (Flags(registry, ref) & 1) == 0;
    }

    // The campaign slot a loaded progress selects: the one after the last completed, 0 in the sandbox.
    int32_t SelectAfterLoad(bool sandbox, int32_t campaignCount) const
    {
        return game_core::SelectAfterLoad(m_slots, sandbox, campaignCount);
    }
    // Marks `ref` completed and unlocks its successor in the campaign or scenario set order.
    void Complete(const Registry& registry, Ref ref, int32_t campaignCount)
    {
        const auto* entry = registry.Find(ref);
        if (!entry) return;
        if (!entry->IsExtra()) {
            game_core::Complete(m_slots, entry->menuSlot, campaignCount);
            return;
        }
        if (entry->IsSandbox()) return;
        const int32_t current = RawExtraFlags(registry, *entry);
        m_extra[entry->id] = (current & ~1) | 8;
        const auto* set = registry.FindSet(entry->setIndex);
        if (!set || set->unlock != scenario_load::SetUnlock::Sequential) return;
        for (size_t i = 0; i + 1 < set->scenarios.size(); ++i) {
            if (set->scenarios[i] != ref) continue;
            const auto* next = registry.Find(set->scenarios[i + 1]);
            if (next && !(RawExtraFlags(registry, *next) & 12)) m_extra[next->id] = 2;
            break;
        }
    }
    void UnlockAll(bool sandbox, int32_t campaignCount) { game_core::UnlockAll(m_slots, sandbox, campaignCount); }
    void UnlockAll(const Registry& registry, bool sandbox, int32_t campaignCount)
    {
        game_core::UnlockAll(m_slots, sandbox, campaignCount);
        for (const auto& entry : registry.Entries())
            if (entry.IsExtra()) m_extra[entry.id] = sandbox ? 8 : 2;
    }

    std::string ToJson() const { return progress_file::ToJson(m_slots, m_extra); }
    // False (progress untouched) for a document or seed file that does not parse.
    bool FromJson(std::string_view text) { return progress_file::FromJson(text, m_slots, &m_extra); }
    bool FromSeedFile(std::string_view bytes)
    {
        if (!progress_file::FromSeedFile(bytes, m_slots)) return false;
        m_extra.clear();
        return true;
    }

private:
    bool RequirementsMet(const Registry& registry, const Registry::ScenarioSet& set) const
    {
        for (const std::string& reqId : set.requiresCompleted) {
            const auto reqRef = registry.FindById(reqId, scenario_load::ScenarioKind::Campaign);
            if (!reqRef || !IsComplete(registry, *reqRef)) return false;
        }
        return true;
    }

    int32_t RawExtraFlags(const Registry& registry, const Registry::Entry& entry) const
    {
        if (const auto it = m_extra.find(entry.id); it != m_extra.end()) return it->second;
        const auto* set = registry.FindSet(entry.setIndex);
        if (!set) return 1;
        if (entry.IsSandbox()) {
            if (set->sandboxUnlock == scenario_load::SandboxUnlock::All) return 4;
            for (const Ref sRef : set->scenarios)
                if (!IsComplete(registry, sRef)) return 1;
            return 2;
        }
        if (set->unlock == scenario_load::SetUnlock::All) return 2;
        if (!set->initiallyAvailable.empty()) {
            const bool init =
                    std::find(set->initiallyAvailable.begin(), set->initiallyAvailable.end(), entry.id) != set->initiallyAvailable.end();
            return init ? 2 : 1;
        }
        if (!set->scenarios.empty() && registry.Find(set->scenarios.front()) == &entry) return 2;
        return 1;
    }

    int32_t Flags(const Registry& registry, Ref ref) const
    {
        const auto* entry = registry.Find(ref);
        if (!entry) return 1;
        if (!entry->IsExtra()) return m_slots[entry->menuSlot];
        const auto* set = registry.FindSet(entry->setIndex);
        if (set && !RequirementsMet(registry, *set)) return 1;
        return RawExtraFlags(registry, *entry);
    }

    int32_t m_slots[game_core::kSlots] = {};
    std::map<std::string, int32_t> m_extra;
};

} // namespace obl::scenario_progress
