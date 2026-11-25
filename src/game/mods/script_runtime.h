#pragma once

#include "core/settings_schema.h"
#include "game/game/command.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The runtime of the presentation scripts of mods (docs/modding/reference/scripting.md): one sandboxed Lua state per mod, outside
// the simulation. A script reaches the world only through the game commands of ScriptCommands, which the host submits like the GUI
// does, so a replay holds what a script did and plays back without the mod. Pure logic over a ScriptHost: no game, no Windows API.
namespace obl::mods {

// An input the script makes up (docs/modding/reference/scripting.md, obl.input): the game sees it like the device's own.
struct SyntheticInput {
    enum class Kind {
        Press, // the button goes down at (x, y)
        Release, // the button goes up at (x, y)
        Click, // down and up at (x, y): the game sees the press in one update and the release in the next
        Move, // the pointer is at (x, y) until the device moves it or Free
        Free, // the pointer is the device's again
    };
    Kind kind = Kind::Move;
    int button = 0; // 0 left, 1 right
    int x = 0;
    int y = 0;
};

// The map view's camera: the top-left corner in map pixels and the zoom level (0 in, 1 normal, 2 out).
struct CameraView {
    int x = 0;
    int y = 0;
    int zoomLevel = 1;
};

// A GUI call (obl.gui): dialogs and controls by name, the object selection and the build tool.
struct GuiRequest {
    enum class Op { ShowControl, HideControl, MoveControl, OpenDialog, CloseDialog, MoveDialog, Select, Place, CancelTool };
    Op op = Op::ShowControl;
    std::string name; // the control id, the dialog name, or the building/scenery key of Place
    std::string kind; // Place: "building" or "scenery"
    int x = 0;
    int y = 0;
    uint32_t id = 0; // Select: the object id
};

// One drawing call of the overlay, in game pixels. Colors are 0xAARRGGBB.
struct OverlayOp {
    enum class Kind { Rect, Frame, Text, Image, View };
    Kind kind = Kind::Rect;
    int x = 0, y = 0, w = 0, h = 0; // the destination
    int sx = 0, sy = 0, sw = 0, sh = 0; // View: the source region of the frame
    uint32_t color = 0;
    std::string text; // Text: UTF-8
    std::string font; // Text: the pack font
    std::filesystem::path image; // Image: a PNG under the mod's overlay/
};

struct ScreenInfo {
    int width = 0;
    int height = 0;
    // The parts of the screen a phone's notch or rounded corners cover, in game pixels.
    int safeLeft = 0, safeTop = 0, safeRight = 0, safeBottom = 0;
};

// An object of the world as scripts read it.
struct ObjectInfo {
    uint32_t id = 0;
    std::string kind; // "building", "guest", "scenery", "vehicle", "staff", "other"
    std::string key; // buildings: the catalog key
    int x = 0; // tile
    int y = 0;
    int state = 0;
};

// What scripts read of the world: a copy taken after the simulation's tick, so reading it cannot change the simulation.
struct GameSnapshot {
    double money = 0;
    int day = 0;
    double hour = 0;
    int speedLevel = 0;
    bool paused = false;
    std::string weather;
    int guests = 0;
    double mood = 0; // the guests' mean happiness; 0 without guests
    std::vector<ObjectInfo> buildings;
    std::optional<ObjectInfo> selected;
    std::optional<ObjectInfo> hovered;
};

// What a script may reach beyond its sandbox. The game supplies the real one (presentation_scripts.cpp), unit tests a fake.
class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    // A line for the game log (print, obl.log, the runtime's own messages).
    virtual void Log(const std::string& line) = 0;
    // A command from ScriptCommands, already parsed and checked against the list.
    virtual void SubmitCommand(command::Command command) = 0;
    // Whether a world is in settled gameplay, so a command submitted now runs at the next tick.
    virtual bool InWorld() const = 0;
    // The text of the setting `key` of `mod` as the player's openbeach.ini holds it (`[Mod.<mod>] <key>`); nullopt when it has none.
    // The runtime checks it against the mod's declared setting and falls back to the default (docs/modding/reference/mods.md#settings).
    virtual std::optional<std::string> ReadSetting(const std::string& mod, const std::string& key) const = 0;

    // The calls of the input, camera, GUI, overlay and information blocks. Each has a default that does nothing (or answers
    // "not available"), so a host offers what it can: the game all of them, a unit test what it checks.
    virtual void SynthesizeInput(const SyntheticInput&) {}
    virtual std::optional<CameraView> Camera() const { return std::nullopt; }
    // False when there is no map view to move (outside a world).
    virtual bool SetCameraPosition(int, int) { return false; }
    virtual bool SetZoomLevel(int) { return false; }
    virtual bool Gui(const GuiRequest&) { return false; }
    virtual void Draw(const OverlayOp&) {}
    // The size of `text` in the pack font `font`; nullopt when the pack has no such font.
    virtual std::optional<std::pair<int, int>> MeasureText(const std::string&, const std::string&) { return std::nullopt; }
    virtual ScreenInfo Screen() const { return {}; }
    // "mouse", "touch" or empty: what moved the pointer last.
    virtual std::string LastPointer() const { return {}; }
    // The game state after the last tick; null outside a world.
    virtual const GameSnapshot* Snapshot() { return nullptr; }
};

struct ScriptLimits {
    // Lua VM instructions one call into a mod may run (a lifecycle function, or loading its files) before it is stopped.
    uint64_t instructionsPerCall = 10'000'000;
    // Bytes the mod's Lua state may hold.
    size_t memoryPerMod = 32u << 20;
    // Bytes of keys and values the mod's store may hold.
    size_t storeBytes = 64u << 10;
};

// A mod whose scripts to run: `directory` holds scripts/presentation/*.lua, `storeFile` its obl.store (kept outside saves).
struct ScriptMod {
    std::string id;
    std::filesystem::path directory;
    std::filesystem::path storeFile;
    std::vector<settings_schema::Field> settings; // what mod.json declares; a script reads them with obl.settings.get
    std::vector<std::string> takesOver; // mod_pack::kTakeOverAreas names from the manifest
};

// An input event for on_input and on_input_observe (docs/modding/reference/scripting.md, "Input events").
struct InputEvent {
    std::string kind; // "key", "mouse", "wheel" or "touch"
    std::string phase; // "down", "up"; "move" (mouse, touch); "cancel" (touch)
    std::string key; // key: a key_names.h name
    std::string button; // mouse down and up: "left" or "right"
    int x = 0; // mouse, wheel and touch: game pixels
    int y = 0;
    int64_t finger = 0; // touch: tells the fingers apart
    double delta = 0; // wheel: notches, positive away from the player
};

class ScriptRuntime {
public:
    explicit ScriptRuntime(ScriptHost& host, ScriptLimits limits = {});
    ~ScriptRuntime(); // Stop
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Loads the scripts of every mod in load order (each mod's files in name order), then calls each mod's on_start in load order.
    // A mod whose files do not load, or whose call fails, is switched off with a message naming the mod, the file and the line;
    // the others keep running. Mods without scripts are skipped.
    void Start(std::span<const ScriptMod> mods, bool checking = false);
    // on_frame(dt) of every running mod in load order.
    void Frame(float dt);
    // on_input(event) of the running mods, the last in load order first, until one consumes the event; the mod that took over the
    // event's area (input.touch for touch, input.mouse_drag for mouse) gets it before the others. Then on_input_observe(event) of
    // every running mod, the last first, with consumed_by. Returns the id of the mod that consumed it, or empty.
    std::string Input(const InputEvent& event);
    // on_action(event) of the mod `modId`: a GUI patch button with the action `Mod:<id>.<event>`.
    void Action(std::string_view modId, std::string_view event);
    // Reads every mod's settings again from the host and calls on_setting(id, value) of each mod for each setting whose value
    // changed since the last read (Start reads them first), in load order. The game calls it after the player changed one.
    void SettingsChanged();
    // on_stop of every running mod from the last to the first, then the stores are written. Start may follow again.
    void Stop();

    // The ids of the mods still running, in load order.
    std::vector<std::string> RunningMods() const;
    // The running mod that has taken over `area` (a kTakeOverAreas name): the last in load order that lists it; empty when none,
    // so the default comes back at once when that mod is switched off.
    std::string AreaOwner(std::string_view area) const;
    // Why mods were switched off since Start, one message each.
    const std::vector<std::string>& Failures() const { return m_failures; }

    struct Mod; // script_runtime.cpp

private:
    // Runs the request's lifecycle function in every running mod; a mod whose call fails is switched off.
    template<class Request> void CallAll(bool reverse, const Request& request);
    void SwitchOff(Mod& mod, const std::string& why);

    ScriptHost& m_host;
    ScriptLimits m_limits;
    // The frame times of the last frames (seconds, the newest last), for obl.stats.
    std::vector<float> m_frameTimes;
    std::vector<std::unique_ptr<Mod>> m_mods;
    std::vector<std::string> m_failures;
};

// The commands scripts may send by their text name (docs/modding/reference/scripting.md, "Commands"); obl.command refuses others.
std::span<const std::string_view> ScriptCommands();

// The script API as the engine registers it: every function and value under `obl` by its full name ("obl.command", "obl.api"),
// sorted. The reference page's list is compared with this (script_runtime_test).
std::vector<std::string> RegisteredApi();

} // namespace obl::mods
