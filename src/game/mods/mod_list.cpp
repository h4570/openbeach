#include "game/mods/mod_list.h"

#include <algorithm>

namespace obl::mods {

ModList::ModList(std::vector<mod_pack::InstalledMod> installed, std::string packId, std::span<const std::string> active, int api)
    : m_packId(std::move(packId)), m_api(api)
{
    for (const std::string& id : active) {
        const auto it = std::find_if(installed.begin(), installed.end(), [&](const auto& mod) { return mod.id == id; });
        if (it == installed.end()) continue;
        m_entries.push_back({std::move(*it), true});
        installed.erase(it);
    }
    for (auto& mod : installed) m_entries.push_back({std::move(mod), false});
    Refresh();
}

const Entry* ModList::Find(const std::string& id) const
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) { return entry.mod.id == id; });
    return it == m_entries.end() ? nullptr : &*it;
}

std::vector<std::string> ModList::ActiveIds() const
{
    std::vector<std::string> ids;
    for (const Entry& entry : m_entries)
        if (entry.on) ids.push_back(entry.mod.id);
    return ids;
}

size_t ModList::ActiveCount() const
{
    return static_cast<size_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& entry) { return entry.on; }));
}

Problem ModList::FixedProblem(const mod_pack::InstalledMod& mod, std::string& detail) const
{
    if (!mod.valid) {
        detail = mod.error;
        return Problem::Invalid;
    }
    const auto& packs = mod.manifest.packs;
    if (std::find(packs.begin(), packs.end(), m_packId) == packs.end()) {
        detail.clear();
        for (const std::string& pack : packs) detail += (detail.empty() ? "" : ", ") + pack;
        return Problem::WrongPack;
    }
    if (mod.manifest.api > m_api) {
        detail = std::to_string(mod.manifest.api);
        return Problem::NeedsApi;
    }
    return Problem::None;
}

bool ModList::SetOn(const std::string& id, bool on, Problem& why, std::string& detail)
{
    why = Problem::None;
    detail.clear();
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) { return entry.mod.id == id; });
    if (it == m_entries.end()) {
        why = Problem::Invalid;
        detail = "not installed";
        return false;
    }
    if (it->on == on) return true;
    if (on) {
        why = FixedProblem(it->mod, detail);
        if (why != Problem::None) return false;
        for (const Entry& other : m_entries)
            if (other.on && other.mod.valid && mod_pack::AreIncompatible(it->mod.manifest, other.mod.manifest)) {
                why = Problem::ConflictsWith;
                detail = other.mod.id;
                return false;
            }
    }
    Entry moved = std::move(*it);
    m_entries.erase(it);
    moved.on = on;
    if (on) {
        // Last of the active mods: a mod switched on wins over the ones already on.
        const auto end = std::find_if(m_entries.begin(), m_entries.end(), [](const Entry& entry) { return !entry.on; });
        m_entries.insert(end, std::move(moved));
    } else {
        const auto at = std::find_if(
                m_entries.begin(), m_entries.end(), [&](const Entry& entry) { return !entry.on && entry.mod.id > moved.mod.id; });
        m_entries.insert(at, std::move(moved));
    }
    Refresh();
    return true;
}

bool ModList::Move(const std::string& id, int delta)
{
    const size_t active = ActiveCount();
    size_t index = 0;
    while (index < active && m_entries[index].mod.id != id) ++index;
    if (index == active || delta == 0) return false;
    const auto target = static_cast<long long>(index) + delta;
    if (target < 0 || target >= static_cast<long long>(active)) return false;
    Entry moved = std::move(m_entries[index]);
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
    m_entries.insert(m_entries.begin() + static_cast<std::ptrdiff_t>(target), std::move(moved));
    Refresh();
    return true;
}

bool ModList::CanStart() const
{
    return std::all_of(m_entries.begin(), m_entries.end(), [](const Entry& entry) { return !entry.on || entry.problem == Problem::None; });
}

void ModList::AddConflictSource(ConflictSource source)
{
    m_sources.push_back(std::move(source));
    Refresh();
}

void ModList::Refresh()
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        Entry& entry = m_entries[i];
        entry.problem = FixedProblem(entry.mod, entry.detail);
        if (entry.problem != Problem::None) continue;
        entry.detail.clear();
        const auto& manifest = entry.mod.manifest;
        // An inactive mod shows what would stop it beside the active ones; an active one what stops it in this order.
        for (const std::string& need : manifest.requiredMods) {
            const Entry* required = Find(need);
            if (!required || !required->on) {
                entry.problem = Problem::MissingRequirement;
                entry.detail = need;
                break;
            }
            if (entry.on && required > &entry) {
                entry.problem = Problem::RequirementLater;
                entry.detail = need;
                break;
            }
        }
        if (entry.problem != Problem::None) continue;
        for (const Entry& other : m_entries)
            if (&other != &entry && other.on && other.mod.valid && mod_pack::AreIncompatible(manifest, other.mod.manifest)) {
                entry.problem = Problem::ConflictsWith;
                entry.detail = other.mod.id;
                break;
            }
    }
    m_conflicts.clear();
    // The sources read the mods' files, so a list that cannot start is not asked about.
    if (!CanStart()) return;
    const std::vector<std::string> ids = ActiveIds();
    if (ids.empty()) return;
    for (const ConflictSource& source : m_sources)
        for (Conflict& conflict : source(ids)) m_conflicts.push_back(std::move(conflict));
}

std::vector<Conflict> FindTakeOverConflicts(const ModList& list, std::span<const std::string> ids)
{
    std::vector<std::string> names;
    for (std::string_view area : mod_pack::kTakeOverAreas) names.emplace_back(area);
    for (const auto& action : key_actions::EngineActions())
        names.push_back(std::string(mod_pack::kTakeOverActionPrefix) + std::string(action.name));
    std::vector<Conflict> found;
    for (const std::string& area : names) {
        std::vector<std::string> takers;
        for (const std::string& id : ids) {
            const Entry* entry = list.Find(id);
            if (!entry || !entry->mod.valid) continue;
            const auto& areas = entry->mod.manifest.takesOver;
            if (std::find(areas.begin(), areas.end(), area) != areas.end()) takers.push_back(id);
        }
        if (takers.size() < 2) continue;
        const std::string& winner = takers.back();
        for (size_t i = 0; i + 1 < takers.size(); ++i) found.push_back({ConflictKind::TakeOver, winner, takers[i], area});
    }
    return found;
}

std::vector<Conflict> FindKeyConflicts(const ModList& list, std::span<const std::string> ids, const key_actions::PlayerLookup& player)
{
    std::vector<mod_pack::ActiveMod> active;
    active.reserve(ids.size());
    for (const std::string& id : ids) {
        const Entry* entry = list.Find(id);
        if (!entry || !entry->mod.valid) continue;
        active.push_back({entry->mod.manifest, {}, entry->mod.directory});
    }
    std::vector<Conflict> found;
    for (std::string& line : mod_pack::KeyConflicts(active, player)) found.push_back({ConflictKind::Key, {}, {}, std::move(line)});
    return found;
}

} // namespace obl::mods
