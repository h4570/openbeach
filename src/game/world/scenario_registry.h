#pragma once

#include "game/world/scenario_load.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obl::scenario_load {

enum class ScenarioKind : uint8_t { Campaign, Sandbox };

// Value handle into ScenarioRegistry; replaces raw (index, sandbox) pairs.
struct ScenarioRef {
    int32_t index = -1;

    constexpr bool IsValid() const { return index >= 0; }
    constexpr bool operator==(const ScenarioRef&) const = default;
};

// Owns the scenarios declared in scenarios/campaign.json with their kind, menu slot, pictures and text keys.
class ScenarioRegistry {
public:
    struct Entry {
        std::string id;
        ScenarioKind kind = ScenarioKind::Campaign;
        int32_t menuSlot = 0; // -1 for extra entries from mod scenario sets
        int32_t setIndex = -1; // -1 for pack entries, index into Sets() for mod sets
        std::string modId; // owning mod id for set entries, empty for pack entries
        ScenarioPictures pictures;
        ScenarioTextKeys textKeys;

        bool IsSandbox() const { return kind == ScenarioKind::Sandbox; }
        bool IsExtra() const { return menuSlot < 0; }
    };

    struct ScenarioSet {
        std::string id;
        std::string modId;
        std::string title;
        std::string description;
        SetUnlock unlock = SetUnlock::Sequential;
        SandboxUnlock sandboxUnlock = SandboxUnlock::All;
        std::vector<std::string> initiallyAvailable;
        std::vector<std::string> requiresCompleted;
        std::vector<ScenarioRef> scenarios;
        std::vector<ScenarioRef> sandboxes;
    };

    static ScenarioRegistry FromCampaign(const Campaign& campaign);
    static bool LoadFromPack(
            const std::filesystem::path& pack, ScenarioRegistry& out, std::string& error, std::vector<std::string>* allErrors = nullptr);

    int32_t CampaignCount() const { return m_campaignCount; }
    int32_t SandboxCount() const { return m_sandboxCount; }
    int32_t ListCount() const { return static_cast<int32_t>(m_listNames.size()); }
    const std::u16string& ListName(int32_t index) const { return m_listNames.at(static_cast<size_t>(index)); }

    const std::vector<Entry>& Entries() const { return m_entries; }
    const std::vector<ScenarioSet>& Sets() const { return m_sets; }
    const ScenarioSet* FindSet(int32_t setIndex) const;
    const Entry& Get(ScenarioRef ref) const { return m_entries.at(static_cast<size_t>(ref.index)); }
    const Entry* Find(ScenarioRef ref) const;

    std::optional<ScenarioRef> FindById(std::string_view id) const;
    std::optional<ScenarioRef> FindById(std::string_view id, ScenarioKind kind) const;
    std::optional<ScenarioRef> FindByMenuSlot(int32_t slot, ScenarioKind kind) const;
    // What `start scenario <name>` of a replay, bot or chaos file names: `<n>` or `s<n>` = campaign menu slot n (1-based),
    // `sandbox<n>` = sandbox menu slot n, otherwise the id of a campaign entry, then of a sandbox entry. Nothing for an unknown name.
    std::optional<ScenarioRef> FindByStartName(std::string_view name) const;
    // The accepted forms of FindByStartName, for error messages.
    static const char* StartNameForms() { return "<n>, s<n>, sandbox<n> or a scenario id"; }

private:
    std::vector<Entry> m_entries;
    std::vector<ScenarioSet> m_sets;
    std::vector<std::u16string> m_listNames;
    int32_t m_campaignCount = 0;
    int32_t m_sandboxCount = 0;
};

} // namespace obl::scenario_load
