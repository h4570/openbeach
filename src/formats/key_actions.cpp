#include "formats/key_actions.h"

#include "core/ascii_case.h"
#include "core/key_code.h"
#include "core/key_names.h"

#include <algorithm>

namespace obl::key_actions {

namespace {

using namespace input;

// The engine's actions with the keys the game always had. Escape and Shift are shared by a mode's actions on purpose: only one
// of the modes is on at a time.
constexpr EngineAction kEngine[kEngineCount] = {
        {"camera.left", "Scroll the map left", true, {Left, 0}},
        {"camera.right", "Scroll the map right", true, {Right, 0}},
        {"camera.up", "Scroll the map up", true, {Up, 0}},
        {"camera.down", "Scroll the map down", true, {Down, 0}},
        {"game.pause", "Pause or resume the game", false, {P, 0}},
        {"panel.load_game", "Open or close the load game panel", false, {F5, 0}},
        {"panel.save_game", "Open or close the save game panel", false, {F4, 0}},
        {"panel.options", "Open or close the options panel", false, {F2, 0}},
        {"panel.music", "Open or close the music panel", false, {M, 0}},
        {"panel.build", "Open or close the build panel", false, {B, 0}},
        {"panel.scenery", "Open or close the scenery panel", false, {S, 0}},
        {"camera.zoom_in", "Zoom the map in one level", false, {PageUp, 0}},
        {"camera.zoom_out", "Zoom the map out one level", false, {PageDown, 0}},
        {"game.speed_up", "Raise the game speed one level", false, {Equals, 0}},
        {"game.speed_down", "Lower the game speed one level", false, {Minus, 0}},
        {"tool.cancel", "Leave the building, scenery, paint or tool mode", false, {Escape, 0}},
        {"place.rotate", "Turn the building or scenery being placed", false, {Space, 0}},
        {"place.keep", "Hold to go on placing after a click", true, {LeftShift, RightShift}},
};

constexpr int kDefaultRank = 0, kPlayerRank = 1000; // a mod's rank is its place in the load order, from 1

struct Slot {
    std::array<uint8_t, kMaxKeys> keys{};
    size_t count = 0;
};

struct State {
    Table table;
    std::array<Slot, kEngineCount> slots{};
    std::array<bool, kEngineCount> taken{};
};

void Fill(State& state, const Table& table)
{
    state.table = table;
    for (size_t i = 0; i < kEngineCount; ++i) {
        Slot slot;
        for (uint8_t key : table.entries[i].keys)
            if (slot.count < kMaxKeys) slot.keys[slot.count++] = key;
        state.slots[i] = slot;
    }
}

State& Global()
{
    static State state = [] {
        State s;
        Fill(s, Resolve({}, nullptr));
        return s;
    }();
    return state;
}

std::string Quoted(std::string_view text)
{
    return "\"" + std::string(text) + "\"";
}

} // namespace

std::span<const EngineAction> EngineActions()
{
    return kEngine;
}

std::optional<Engine> FindEngine(std::string_view name)
{
    for (size_t i = 0; i < kEngineCount; ++i)
        if (kEngine[i].name == name) return static_cast<Engine>(i);
    return std::nullopt;
}

bool ParseKeys(std::string_view text, KeyList& keys, std::string& error)
{
    keys.clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(',', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view name = text.substr(start, end - start);
        start = end + 1;
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
        if (name.empty() || ascii::EqualsNoCase(name, "none")) continue;
        const uint8_t key = FindKey(name);
        if (key == 0) {
            error = "no key is named " + Quoted(name);
            return false;
        }
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    }
    if (keys.size() > kMaxKeys) {
        error = "more than " + std::to_string(kMaxKeys) + " keys";
        return false;
    }
    return true;
}

Table Resolve(std::span<const ModKeys> mods, const PlayerLookup& player)
{
    struct Work {
        Entry entry;
        int rank = kDefaultRank;
        std::string source; // what to call the layer in a conflict line
    };
    Table table;
    std::vector<Work> work;
    for (const EngineAction& action : kEngine) {
        Work w;
        w.entry.name = std::string(action.name);
        w.entry.held = action.held;
        for (uint8_t key : action.keys)
            if (key) w.entry.keys.push_back(key);
        w.source = "the default";
        work.push_back(std::move(w));
    }
    int rank = 0;
    for (const ModKeys& mod : mods) {
        ++rank;
        for (const ModAction& action : mod.actions) {
            Work w;
            w.entry.name = mod.mod + "." + action.id;
            w.entry.mod = mod.mod;
            w.entry.id = action.id;
            w.entry.keys = action.keys;
            w.rank = rank;
            w.source = "mod " + Quoted(mod.mod);
            work.push_back(std::move(w));
        }
    }
    const auto find = [&](std::string_view name) -> Work* {
        for (Work& w : work)
            if (w.entry.name == name) return &w;
        return nullptr;
    };
    rank = 0;
    for (const ModKeys& mod : mods) {
        ++rank;
        for (const Binding& binding : mod.bindings) {
            Work* w = find(binding.action);
            if (!w) {
                table.problems.push_back("mod " + Quoted(mod.mod) + " binds " + Quoted(binding.action) + ", which is no action");
                continue;
            }
            w->entry.keys = binding.keys;
            w->rank = rank;
            w->source = "mod " + Quoted(mod.mod);
        }
    }
    if (player) {
        for (Work& w : work) {
            const auto value = player(w.entry.name);
            if (!value || value->empty()) continue;
            KeyList keys;
            std::string error;
            if (!ParseKeys(*value, keys, error)) {
                table.problems.push_back("[Keys] " + w.entry.name + ": " + error);
                continue;
            }
            w.entry.keys = keys;
            w.rank = kPlayerRank;
            w.source = "the player's binding";
        }
    }
    // One key, one action: the stronger layer keeps it, inside a layer the later action.
    for (int key = 1; key < 256; ++key) {
        std::vector<size_t> claimants;
        for (size_t i = 0; i < work.size(); ++i)
            if (std::find(work[i].entry.keys.begin(), work[i].entry.keys.end(), key) != work[i].entry.keys.end()) claimants.push_back(i);
        if (claimants.size() < 2) continue;
        size_t winner = claimants[0];
        for (size_t i : claimants)
            if (work[i].rank >= work[winner].rank) winner = i;
        std::string names;
        for (size_t i : claimants) names += (names.empty() ? "" : ", ") + Quoted(work[i].entry.name);
        table.conflicts.push_back("key " + Quoted(KeyName(static_cast<uint8_t>(key))) + " is bound to " + names + "; " +
                Quoted(work[winner].entry.name) + " wins (" + work[winner].source + ")");
        for (size_t i : claimants) {
            if (i == winner) continue;
            auto& keys = work[i].entry.keys;
            keys.erase(std::remove(keys.begin(), keys.end(), static_cast<uint8_t>(key)), keys.end());
        }
    }
    for (Work& w : work) table.entries.push_back(std::move(w.entry));
    return table;
}

void Install(const Table& table)
{
    Fill(Global(), table);
}

const Table& Current()
{
    return Global().table;
}

std::span<const uint8_t> Keys(Engine action)
{
    State& state = Global();
    const size_t i = static_cast<size_t>(action);
    if (state.taken[i]) return {};
    return {state.slots[i].keys.data(), state.slots[i].count};
}

void SetTakenOver(Engine action, bool taken)
{
    Global().taken[static_cast<size_t>(action)] = taken;
}

bool IsTakenOver(Engine action)
{
    return Global().taken[static_cast<size_t>(action)];
}

std::vector<const Entry*> ModActionsFor(uint8_t key)
{
    std::vector<const Entry*> found;
    for (const Entry& entry : Global().table.entries)
        if (!entry.mod.empty() && std::find(entry.keys.begin(), entry.keys.end(), key) != entry.keys.end()) found.push_back(&entry);
    return found;
}

} // namespace obl::key_actions
