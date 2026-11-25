#pragma once

#include "formats/mod_pack.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

// What the mods screen edits (docs/modding/players/mods-screen.md): the installed mods, which of them are on and in what order, why
// a mod cannot start, and the conflicts of the list. Pure logic over mod_pack's manifests: no game, no files beyond what the caller
// read, so the unit tests run it on made-up mods.
namespace obl::mods {

// Where two active mods want the same thing; the later one in load order wins (docs/modding/players/mods-screen.md#conflicts).
enum class ConflictKind {
    Value, // the same value of a data file patched by both (obl.json_patch)
    TakeOver, // the same default behavior taken over (`takes_over` in mod.json)
    Key, // the same key bound to more than one action, or an unusable key binding (mod_pack::KeyConflicts)
};

struct Conflict {
    ConflictKind kind = ConflictKind::Value;
    std::string winner, loser; // mod ids: the winner comes later in the load order (empty when `what` already names the winner)
    std::string what; // what conflicts, as the player reads it (`rules/game_parameters.json /cash` or a key conflict line)
};

// Finds the conflicts of one kind for a list of mods in load order.
using ConflictSource = std::function<std::vector<Conflict>(std::span<const std::string> ids)>;

// Why a mod cannot start on this list.
enum class Problem {
    None,
    Invalid, // no or a broken mod.json; `detail` is the reason
    WrongPack, // made for other packs; `detail` names them
    NeedsApi, // needs a newer presentation script API; `detail` is the version
    NeedsEngine, // `engine_min` is newer than the game; `detail` is that version
    MissingRequirement, // `detail` names a required mod that is off
    RequirementLater, // `detail` names a required mod that comes after it
    ConflictsWith, // `detail` names an active mod that one of the two lists in `conflicts`
};

struct Entry {
    mod_pack::InstalledMod mod;
    bool on = false;
    Problem problem = Problem::None;
    std::string detail;
};

class ModList {
public:
    // `active` is the list the game runs with (or the one the player last saved), in load order; ids that are not installed are
    // dropped. `api` is the newest presentation script API the game offers, `engine` the game's version (`engine_min` is checked
    // against it).
    ModList(std::vector<mod_pack::InstalledMod> installed, std::string packId, std::span<const std::string> active, int api,
            SemVer engine = EngineVersion());

    // The active mods first, in load order, then the others by id.
    const std::vector<Entry>& Entries() const { return m_entries; }
    const Entry* Find(const std::string& id) const;
    // The ids of the active mods, in load order.
    std::vector<std::string> ActiveIds() const;
    size_t ActiveCount() const;

    // Switches a mod on (it goes last in the order) or off. False with `why` and `detail` (as in Entry) when it cannot be switched
    // on: invalid, made for another pack, needs a newer game, or it and an active mod name each other in `conflicts`.
    bool SetOn(const std::string& id, bool on, Problem& why, std::string& detail);
    // Moves an active mod `delta` places in the load order; false when it is not active or is already at that end.
    bool Move(const std::string& id, int delta);

    // Whether every active mod can start in this order.
    bool CanStart() const;

    // Conflicts are found by the sources, the first added first; they are asked again after every change.
    void AddConflictSource(ConflictSource source);
    const std::vector<Conflict>& Conflicts() const { return m_conflicts; }

private:
    void Refresh();
    Problem FixedProblem(const mod_pack::InstalledMod& mod, std::string& detail) const;

    std::vector<Entry> m_entries;
    std::string m_packId;
    int m_api = 0;
    SemVer m_engine;
    std::vector<ConflictSource> m_sources;
    std::vector<Conflict> m_conflicts;
};

// Finds input/camera/action default areas (`takes_over` in mod.json) claimed by more than one mod in `ids` (load order); the later
// mod in load order wins over each earlier one.
std::vector<Conflict> FindTakeOverConflicts(const ModList& list, std::span<const std::string> ids);

// Finds key conflicts and unusable bindings for `ids` (in load order) via `mod_pack::KeyConflicts`, with optional player `[Keys]`
// overrides.
std::vector<Conflict> FindKeyConflicts(
        const ModList& list, std::span<const std::string> ids, const key_actions::PlayerLookup& player = nullptr);

} // namespace obl::mods
