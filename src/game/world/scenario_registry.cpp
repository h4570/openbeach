#include "game/world/scenario_registry.h"

#include "game/game/game_core.h"

namespace obl::scenario_load {
namespace {

ScenarioTextKeys MenuTextKeys(int32_t slot, ScenarioKind kind)
{
    const std::string prefix = kind == ScenarioKind::Sandbox ? "MAINSS_SANDBOX" : "MAINSS_SCENARIO";
    const std::string number = std::to_string(slot + 1);
    return {prefix + number + "_TITLE", prefix + number + "_DESC"};
}

ScenarioRegistry::Entry MakeEntry(const std::string& id, ScenarioKind kind, int32_t slot)
{
    ScenarioRegistry::Entry entry;
    entry.id = id;
    entry.kind = kind;
    entry.menuSlot = slot;
    entry.pictures.frame = slot;
    entry.textKeys = MenuTextKeys(slot, kind);
    return entry;
}

} // namespace

ScenarioRegistry ScenarioRegistry::FromCampaign(const Campaign& campaign)
{
    ScenarioRegistry registry;
    registry.m_campaignCount = static_cast<int32_t>(campaign.campaign.size());
    registry.m_sandboxCount = static_cast<int32_t>(campaign.sandboxes.size());
    registry.m_entries.reserve(campaign.campaign.size() + campaign.sandboxes.size());

    for (size_t i = 0; i < campaign.campaign.size(); ++i)
        registry.m_entries.push_back(MakeEntry(campaign.campaign[i], ScenarioKind::Campaign, static_cast<int32_t>(i)));
    for (size_t i = 0; i < campaign.sandboxes.size(); ++i)
        registry.m_entries.push_back(MakeEntry(campaign.sandboxes[i], ScenarioKind::Sandbox, static_cast<int32_t>(i)));

    for (const std::string& name : game_core::ScenarioListNames(registry.m_campaignCount, registry.m_sandboxCount))
        registry.m_listNames.emplace_back(name.begin(), name.end());
    return registry;
}

bool ScenarioRegistry::LoadFromPack(const std::filesystem::path& pack, ScenarioRegistry& out, std::string& error)
{
    Campaign campaign;
    if (!LoadCampaign(pack, campaign, error)) return false;
    out = FromCampaign(campaign);
    return true;
}

const ScenarioRegistry::Entry* ScenarioRegistry::Find(ScenarioRef ref) const
{
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= m_entries.size()) return nullptr;
    return &m_entries[static_cast<size_t>(ref.index)];
}

std::optional<ScenarioRef> ScenarioRegistry::FindById(std::string_view id) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == id) return ScenarioRef{static_cast<int32_t>(i)};
    return std::nullopt;
}

std::optional<ScenarioRef> ScenarioRegistry::FindById(std::string_view id, ScenarioKind kind) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].kind == kind && m_entries[i].id == id) return ScenarioRef{static_cast<int32_t>(i)};
    return std::nullopt;
}

std::optional<ScenarioRef> ScenarioRegistry::FindByMenuSlot(int32_t slot, ScenarioKind kind) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].kind == kind && m_entries[i].menuSlot == slot) return ScenarioRef{static_cast<int32_t>(i)};
    return std::nullopt;
}

} // namespace obl::scenario_load
