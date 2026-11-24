#pragma once

#include "game/game/command.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The runtime of the presentation scripts of mods (docs/modding/reference/scripting.md): one sandboxed Lua state per mod, outside
// the simulation. A script reaches the world only through the game commands of ScriptCommands, which the host submits like the GUI
// does, so a replay holds what a script did and plays back without the mod. Pure logic over a ScriptHost: no game, no Windows API.
namespace obl::mods {

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
};

// An input event for on_input. `kind` is "key" (with `key`, a key_names.h name) or "mouse" (with `button` and the position).
struct InputEvent {
    std::string kind;
    std::string phase; // "down" or "up"
    std::string key;
    std::string button; // "left" or "right"
    int x = 0;
    int y = 0;
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
    void Start(std::span<const ScriptMod> mods);
    // on_frame(dt) of every running mod in load order.
    void Frame(float dt);
    // on_input(event) of every running mod, the last in load order first (it wins input, docs/modding/reference/scripting.md).
    void Input(const InputEvent& event);
    // on_stop of every running mod from the last to the first, then the stores are written. Start may follow again.
    void Stop();

    // The ids of the mods still running, in load order.
    std::vector<std::string> RunningMods() const;
    // Why mods were switched off since Start, one message each.
    const std::vector<std::string>& Failures() const { return m_failures; }

    struct Mod; // script_runtime.cpp

private:
    // Runs the request's lifecycle function in every running mod; a mod whose call fails is switched off.
    template<class Request> void CallAll(bool reverse, const Request& request);
    void SwitchOff(Mod& mod, const std::string& why);

    ScriptHost& m_host;
    ScriptLimits m_limits;
    std::vector<std::unique_ptr<Mod>> m_mods;
    std::vector<std::string> m_failures;
};

// The commands scripts may send by their text name (docs/modding/reference/scripting.md, "Commands"); obl.command refuses others.
std::span<const std::string_view> ScriptCommands();

// The script API as the engine registers it: every function and value under `obl` by its full name ("obl.command", "obl.api"),
// sorted. The reference page's list is compared with this (script_runtime_test).
std::vector<std::string> RegisteredApi();

} // namespace obl::mods
