#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Named game actions and the keys bound to them (docs/modding/reference/actions.md). The engine's own hotkeys are actions with
// default keys; a mod declares actions of its own and may move any action to other keys; the player's `[Keys]` section of
// openbeach.ini beats both. `Resolve` turns those layers into one table, where a key belongs to one action only. Pure logic over key
// codes (core/key_code.h): nothing here reads a device or reaches the simulation, so bindings never change a world hash.
namespace obl::key_actions {

// The engine's actions, in the order of `EngineActions()`. The game reads its hotkeys through these.
enum class Engine : uint8_t {
    CameraLeft,
    CameraRight,
    CameraUp,
    CameraDown,
    GamePause,
    PanelLoadGame,
    PanelSaveGame,
    PanelOptions,
    PanelMusic,
    PanelBuild,
    PanelScenery,
    CameraZoomIn,
    CameraZoomOut,
    GameSpeedUp,
    GameSpeedDown,
    ToolCancel,
    PlaceRotate,
    PlaceKeep,
    Count,
};
inline constexpr size_t kEngineCount = static_cast<size_t>(Engine::Count);
inline constexpr size_t kMaxKeys = 4; // keys one action may have

struct EngineAction {
    std::string_view name; // "game.pause": what a mod or the ini names
    std::string_view summary;
    bool held; // acts while the key is down (camera, place.keep); the others act once per press
    std::array<uint8_t, 2> keys; // the default keys, 0 for none
};

std::span<const EngineAction> EngineActions();
std::optional<Engine> FindEngine(std::string_view name);

using KeyList = std::vector<uint8_t>;

// An action a mod declares: its name is "<mod>.<id>"; a press calls the mod's on_action(id).
struct ModAction {
    std::string id;
    std::string label;
    KeyList keys; // the default keys
};
// A mod moving an action (its own, another mod's or the engine's) to other keys: it replaces that action's keys.
struct Binding {
    std::string action;
    KeyList keys;
};
struct ModKeys {
    std::string mod;
    std::vector<ModAction> actions;
    std::vector<Binding> bindings;
};

struct Entry {
    std::string name;
    std::string mod; // empty for an engine action
    std::string id; // the mod's own id of the action; empty for an engine action
    bool held = false;
    KeyList keys; // the keys that act after conflicts are settled
};

struct Table {
    std::vector<Entry> entries; // the engine's actions first, then each mod's in load order
    std::vector<std::string> conflicts; // one line per key bound to several actions, naming the winner
    std::vector<std::string> problems; // bindings that name no action, or keys no key has; they are skipped
};

// Parses "F5", "P, F5" or "none" (no key) with the names of core/key_names.h; false with `error` for a name no key has or more
// than kMaxKeys keys.
bool ParseKeys(std::string_view text, KeyList& keys, std::string& error);

// The player's value for an action name (the `[Keys]` entry), or nullopt for the default; empty counts as unset.
using PlayerLookup = std::function<std::optional<std::string>(std::string_view name)>;

// Settles the layers, weakest first: the default keys, each mod's declared keys and bindings in load order, then the player's. When
// several actions end up with one key, the strongest layer keeps it (the player, then the later mod, then the earlier one, then the
// default); inside one layer the later action does. The losers lose that key and `conflicts` says so.
Table Resolve(std::span<const ModKeys> mods, const PlayerLookup& player);

// The rules of a player's rebinding (obl.actions.bind): `keys` is what a `[Keys]` line may hold, or "default" to drop the player's line.
// False with `error` for an action the table lacks, a key name no key has, or a key another action holds now. `current` is the table
// the game reads, `defaults` the one with no player lines; the keys of the action itself never conflict, and "none" always passes.
// The `[Keys]` line of a hand edit would take such a key from the other action; a rebinding screen must not do that unasked.
bool CheckBind(const Table& current, const Table& defaults, std::string_view action, std::string_view keys, std::string& error);

// The table the game reads. Before the first Install it holds the engine's default keys, so a game with no mods behaves as it did.
void Install(const Table& table);
const Table& Current();

// The keys that act for an engine action: empty while a mod has taken the action over (`takes_over` "action.<name>").
std::span<const uint8_t> Keys(Engine action);
void SetTakenOver(Engine action, bool taken);
bool IsTakenOver(Engine action);

// The mod actions with this key, in table order (at most one after conflicts are settled).
std::vector<const Entry*> ModActionsFor(uint8_t key);

} // namespace obl::key_actions
