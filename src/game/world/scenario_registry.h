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

struct ScenarioPictures {
    std::string largeSprite = "ui3_fe_is_pics_large";
    std::string smallSprite = "ui3_fe_is_pics_small";
    int32_t frame = 0;
};

struct ScenarioTextKeys {
    std::string title;
    std::string description;
};

// Owns the scenarios declared in scenarios/campaign.json with their kind, menu slot, pictures and text keys.
class ScenarioRegistry {
public:
    struct Entry {
        std::string id;
        ScenarioKind kind = ScenarioKind::Campaign;
        int32_t menuSlot = 0;
        ScenarioPictures pictures;
        ScenarioTextKeys textKeys;

        bool IsSandbox() const { return kind == ScenarioKind::Sandbox; }
    };

    static ScenarioRegistry FromCampaign(const Campaign& campaign);
    static bool LoadFromPack(const std::filesystem::path& pack, ScenarioRegistry& out, std::string& error);

    int32_t CampaignCount() const { return m_campaignCount; }
    int32_t SandboxCount() const { return m_sandboxCount; }
    int32_t ListCount() const { return static_cast<int32_t>(m_listNames.size()); }
    const std::u16string& ListName(int32_t index) const { return m_listNames.at(static_cast<size_t>(index)); }

    const std::vector<Entry>& Entries() const { return m_entries; }
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
    std::vector<std::u16string> m_listNames;
    int32_t m_campaignCount = 0;
    int32_t m_sandboxCount = 0;
};

} // namespace obl::scenario_load
