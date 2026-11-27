#include "game/world/scenario_registry.h"

#include "core/pack_path.h"
#include "formats/mod_pack.h"
#include "game/game/game_core.h"

#include <algorithm>
#include <set>

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
    const SpritePicture small{"ui3_fe_is_pics_small", slot};
    entry.pictures = {SpritePicture{"ui3_fe_is_pics_large", slot}, small, small};
    entry.textKeys = MenuTextKeys(slot, kind);
    return entry;
}

ScenarioRegistry::Entry MakeExtraEntry(const std::string& id, ScenarioKind kind, int32_t setIndex, const std::string& modId)
{
    ScenarioRegistry::Entry entry;
    entry.id = id;
    entry.kind = kind;
    entry.menuSlot = -1;
    entry.setIndex = setIndex;
    entry.modId = modId;
    const SpritePicture small{"ui3_fe_is_pics_small", 0};
    entry.pictures = {SpritePicture{"ui3_fe_is_pics_large", 0}, small, small};
    return entry;
}

bool IsRegularFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
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

bool ScenarioRegistry::LoadFromPack(
        const std::filesystem::path& pack, ScenarioRegistry& out, std::string& error, std::vector<std::string>* allErrors)
{
    error.clear();
    const auto fail = [&](const std::string& message) {
        if (error.empty()) error = message;
        if (allErrors) allErrors->push_back(message);
    };

    Campaign campaign;
    if (!LoadCampaign(pack, campaign, error)) {
        if (allErrors) allErrors->push_back(error);
        return false;
    }
    auto registry = FromCampaign(campaign);
    for (Entry& entry : registry.m_entries) {
        std::string entryError;
        if (!LoadPresentation(pack, entry.id, entry.textKeys, entry.pictures, entryError)) {
            fail(entryError);
            if (!allErrors) return false;
        }
    }

    // Discover mod scenario sets (`scenarios/sets/*.json`) over active mod layers, in mod load order then set stem order.
    const auto stems = pack_path::ListStems(pack, "scenarios/sets", ".json");
    const auto& activeMods = mod_pack::Active();
    struct DiscoveredSet {
        size_t modIndex = 0;
        std::string stem;
        std::filesystem::path file;
    };
    std::vector<DiscoveredSet> discovered;
    for (const std::string& stem : stems) {
        const auto rel = std::filesystem::path("scenarios/sets") / (stem + ".json");
        for (size_t i = activeMods.size(); i > 0; --i) {
            const auto candidate = activeMods[i - 1].directory / rel;
            if (IsRegularFile(candidate)) {
                discovered.push_back({i - 1, stem, candidate});
                break;
            }
        }
    }
    std::sort(discovered.begin(), discovered.end(), [](const DiscoveredSet& a, const DiscoveredSet& b) {
        return a.modIndex != b.modIndex ? a.modIndex < b.modIndex : a.stem < b.stem;
    });

    std::set<std::string> seenSetIds;
    std::set<std::string> seenScenarioIds;
    for (const Entry& entry : registry.m_entries) seenScenarioIds.insert(entry.id);

    for (const DiscoveredSet& item : discovered) {
        const auto& mod = activeMods[item.modIndex];
        const std::string prefix = "mod \"" + mod.manifest.id + "\" (scenarios/sets/" + item.stem + ".json): ";
        ScenarioSetDefinition def;
        std::string setError;
        if (!LoadScenarioSet(item.file, def, setError)) {
            fail(prefix + setError);
            if (!allErrors) return false;
            continue;
        }
        if (!HasModPrefix(def.id, mod.manifest.id)) {
            fail(prefix + "set id \"" + def.id + "\" must start with \"" + ModPrefix(mod.manifest.id) + "\"");
            if (!allErrors) return false;
            continue;
        }
        if (!seenSetIds.insert(def.id).second) {
            fail(prefix + "duplicate scenario set id \"" + def.id + "\"");
            if (!allErrors) return false;
            continue;
        }
        bool setValid = true;
        for (const std::string& req : def.requiresCompleted) {
            const auto reqRef = registry.FindById(req, ScenarioKind::Campaign);
            if (!reqRef || registry.Get(*reqRef).IsExtra()) {
                fail(prefix + "requires_completed \"" + req + "\" is not a pack campaign scenario");
                setValid = false;
                if (!allErrors) return false;
            }
        }
        for (const std::string& init : def.initiallyAvailable) {
            if (std::find(def.scenarios.begin(), def.scenarios.end(), init) == def.scenarios.end()) {
                fail(prefix + "initially_available \"" + init + "\" is not in this set's scenarios");
                setValid = false;
                if (!allErrors) return false;
            }
        }

        const int32_t setIndex = static_cast<int32_t>(registry.m_sets.size());
        ScenarioSet set;
        set.id = def.id;
        set.modId = mod.manifest.id;
        set.title = def.title;
        set.description = def.description;
        set.unlock = def.unlock;
        set.sandboxUnlock = def.sandboxUnlock;
        set.initiallyAvailable = def.initiallyAvailable;
        set.requiresCompleted = def.requiresCompleted;

        auto addEntries = [&](const std::vector<std::string>& ids, ScenarioKind kind, std::vector<ScenarioRef>& target) {
            for (const std::string& id : ids) {
                if (!HasModPrefix(id, mod.manifest.id)) {
                    fail(prefix + "scenario id \"" + id + "\" must start with \"" + ModPrefix(mod.manifest.id) + "\"");
                    setValid = false;
                    if (!allErrors) return false;
                    continue;
                }
                if (!seenScenarioIds.insert(id).second) {
                    fail(prefix + "duplicate scenario id \"" + id + "\"");
                    setValid = false;
                    if (!allErrors) return false;
                    continue;
                }
                const auto modScenarioFile = mod.directory / "scenarios" / id / "scenario.json";
                if (!IsRegularFile(modScenarioFile)) {
                    fail(prefix + "scenario \"" + id + "\" is missing scenarios/" + id + "/scenario.json in mod \"" + mod.manifest.id +
                            "\"");
                    setValid = false;
                    if (!allErrors) return false;
                    continue;
                }
                Setup setup;
                std::string loadError;
                if (!LoadPack(pack, id, setup, loadError)) {
                    fail(prefix + loadError);
                    setValid = false;
                    if (!allErrors) return false;
                    continue;
                }
                std::string scriptText;
                if (!ReadWorldScript(pack, setup.script, scriptText, loadError)) {
                    fail(prefix + "scenario \"" + id + "\": " + loadError);
                    setValid = false;
                    if (!allErrors) return false;
                    continue;
                }
                Entry entry = MakeExtraEntry(id, kind, setIndex, mod.manifest.id);
                entry.textKeys.title = setup.title;
                entry.textKeys.description = setup.description;
                if (!LoadPresentation(pack, id, entry.textKeys, entry.pictures, loadError)) {
                    fail(prefix + loadError);
                    setValid = false;
                    if (!allErrors) return false;
                    continue;
                }
                const ScenarioRef ref{static_cast<int32_t>(registry.m_entries.size())};
                registry.m_entries.push_back(std::move(entry));
                target.push_back(ref);
            }
            return true;
        };

        if (!addEntries(def.scenarios, ScenarioKind::Campaign, set.scenarios)) return false;
        if (!addEntries(def.sandboxes, ScenarioKind::Sandbox, set.sandboxes)) return false;
        if (setValid) registry.m_sets.push_back(std::move(set));
    }

    if (!error.empty()) return false;
    out = std::move(registry);
    return true;
}

const ScenarioRegistry::ScenarioSet* ScenarioRegistry::FindSet(int32_t setIndex) const
{
    if (setIndex < 0 || static_cast<size_t>(setIndex) >= m_sets.size()) return nullptr;
    return &m_sets[static_cast<size_t>(setIndex)];
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
    if (slot < 0) return std::nullopt;
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].kind == kind && m_entries[i].menuSlot == slot) return ScenarioRef{static_cast<int32_t>(i)};
    return std::nullopt;
}

std::optional<ScenarioRef> ScenarioRegistry::FindByStartName(std::string_view name) const
{
    // A 1-based decimal without sign, zero or leading zeros ("s01" is not slot 1).
    const auto slotOf = [](std::string_view digits) -> std::optional<int32_t> {
        if (digits.empty() || digits.size() > 6 || digits.front() == '0') return std::nullopt;
        int32_t value = 0;
        for (const char c : digits) {
            if (c < '0' || c > '9') return std::nullopt;
            value = value * 10 + (c - '0');
        }
        return value - 1;
    };
    if (name.starts_with("sandbox")) {
        if (const auto slot = slotOf(name.substr(7))) return FindByMenuSlot(*slot, ScenarioKind::Sandbox);
    } else if (const auto slot = slotOf(name.starts_with('s') ? name.substr(1) : name)) {
        return FindByMenuSlot(*slot, ScenarioKind::Campaign);
    }
    if (const auto ref = FindById(name, ScenarioKind::Campaign)) return ref;
    return FindById(name, ScenarioKind::Sandbox);
}

} // namespace obl::scenario_load
