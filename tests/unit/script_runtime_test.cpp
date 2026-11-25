// The presentation scripts of mods (src/game/mods/script_runtime.h): load order, errors that switch off one mod, the instruction
// budget, the memory cap, the sandbox, the command list, the store, and the reference page's lists against the engine's.

#include "unit_test.h"

#include "core/key_code.h"
#include "game/game/command.h"
#include "game/game/start_settings.h"
#include "game/mods/key_names.h"
#include "game/mods/mod_settings_file.h"
#include "game/mods/script_runtime.h"
#include "temp_dir.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace obl;

namespace {

struct FakeHost : mods::ScriptHost {
    std::vector<std::string> lines;
    std::vector<std::string> commands; // text form
    bool inWorld = true;
    std::map<std::string, std::string> settings; // "<mod>.<key>" -> the text the player's openbeach.ini would hold
    std::optional<std::string> ReadSetting(const std::string& mod, const std::string& key) const override
    {
        const auto it = settings.find(mod + "." + key);
        return it == settings.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    void Log(const std::string& line) override { lines.push_back(line); }
    void SubmitCommand(command::Command command) override { commands.push_back(command::Format(command)); }
    bool InWorld() const override { return inWorld; }
    bool Logged(const std::string& line) const { return std::find(lines.begin(), lines.end(), line) != lines.end(); }
};

void Write(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A mod with one script file `main.lua` under `game`/mods/`id`.
mods::ScriptMod Mod(
        const std::filesystem::path& game, const std::string& id, const std::string& script, const std::string& file = "main.lua")
{
    const auto directory = game / "mods" / id;
    Write(directory / "scripts" / "presentation" / file, script);
    return {id, directory, game / "mod-data" / (id + ".json")};
}

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

std::string ReadText(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The `name` of every table row "| `name` |" in the section `heading` (up to the next "## ") of a reference page.
std::vector<std::string> ListedNames(const std::string& page, const std::string& heading, const std::regex& row)
{
    std::vector<std::string> names;
    bool inside = false;
    std::istringstream lines(page);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("## ", 0) == 0) inside = line == heading;
        std::smatch match;
        if (inside && std::regex_search(line, match, row)) names.push_back(match[1]);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

OBL_TEST_CASE("script_runtime.mods_run_in_load_order")
{
    test::TempPath game("scripts-order");
    const std::string script = R"(
        local id = ...
        function on_start() print("start") end
        function on_frame(dt) print("frame " .. dt) end
        function on_input(e) print("input " .. e.kind .. " " .. e.phase .. " " .. e.key) end
        function on_stop() print("stop") end)";
    const std::vector<mods::ScriptMod> list = {
            Mod(game.path, "a", script), Mod(game.path, "b", script), {"no_scripts", game.path / "mods" / "no_scripts", {}}};
    FakeHost host;
    {
        mods::ScriptRuntime runtime(host);
        runtime.Start(list);
        const std::vector<std::string> running = {"a", "b"}; // a mod without scripts takes no state
        OBL_CHECK(runtime.RunningMods() == running);
        runtime.Frame(0.5f);
        runtime.Input({"key", "down", "F5", "", 0, 0});
    } // the destructor stops them
    std::vector<std::string> calls;
    for (const std::string& line : host.lines)
        if (line.rfind("mod ", 0) == 0) calls.push_back(line);
    const std::vector<std::string> expected = {"mod a: start", "mod b: start", "mod a: frame 0.5", "mod b: frame 0.5",
            // The later mod gets input first, and stops first.
            "mod b: input key down F5", "mod a: input key down F5", "mod b: stop", "mod a: stop"};
    OBL_CHECK(calls == expected);
}

OBL_TEST_CASE("script_runtime.files_load_in_name_order_into_one_state")
{
    test::TempPath game("scripts-files");
    Mod(game.path, "m", "names = (names or '') .. 'b'", "b.lua");
    Mod(game.path, "m", "names = (names or '') .. 'a'", "a.lua");
    const auto mod = Mod(game.path, "m", "function on_start() print(names) end", "c.lua");
    Write(mod.directory / "scripts" / "presentation" / "notes.txt", "not a script");
    FakeHost host;
    mods::ScriptRuntime runtime(host);
    runtime.Start(std::vector<mods::ScriptMod>{mod});
    OBL_CHECK(host.Logged("mod m: ab"));
}

OBL_TEST_CASE("script_runtime.an_error_switches_off_only_its_mod")
{
    test::TempPath game("scripts-error");
    const std::vector<mods::ScriptMod> list = {Mod(game.path, "broken", "function on_frame()\n  error('boom')\nend"),
            Mod(game.path, "syntax", "function on_frame(\n"), Mod(game.path, "fine", "function on_frame() print('ok') end")};
    FakeHost host;
    mods::ScriptRuntime runtime(host);
    runtime.Start(list);
    const std::vector<std::string> afterLoad = {"broken", "fine"};
    OBL_CHECK(runtime.RunningMods() == afterLoad);
    runtime.Frame(0.02f);
    runtime.Frame(0.02f);
    const std::vector<std::string> left = {"fine"};
    OBL_CHECK(runtime.RunningMods() == left);
    OBL_REQUIRE(runtime.Failures().size() == 2u);
    // The message names the mod, the file and the line.
    OBL_CHECK(Contains(runtime.Failures()[0], "\"syntax\""));
    OBL_CHECK(Contains(runtime.Failures()[0], "scripts/presentation/main.lua:"));
    OBL_CHECK(Contains(runtime.Failures()[1], "\"broken\""));
    OBL_CHECK(Contains(runtime.Failures()[1], "scripts/presentation/main.lua:2: boom"));
    OBL_CHECK(std::count(host.lines.begin(), host.lines.end(), "mod fine: ok") == 2);
}

OBL_TEST_CASE("script_runtime.the_instruction_budget_stops_an_endless_loop")
{
    test::TempPath game("scripts-budget");
    mods::ScriptLimits limits;
    limits.instructionsPerCall = 100000;
    const std::vector<mods::ScriptMod> list = {Mod(game.path, "loop", "function on_frame()\n  while true do end\nend"),
            // Catching the error does not help: every instruction after it fails as well.
            Mod(game.path, "catch", "function on_frame() while true do pcall(function() while true do end end) end end"),
            Mod(game.path, "tail", "function on_frame() return pcall(function() while true do end end) end"),
            Mod(game.path, "load", "while true do end"), Mod(game.path, "fine", "function on_frame() for i = 1, 1000 do end end")};
    FakeHost host;
    mods::ScriptRuntime runtime(host, limits);
    runtime.Start(list);
    runtime.Frame(0.02f);
    runtime.Frame(0.02f); // the budget is per call: the next frame starts afresh
    const std::vector<std::string> left = {"fine"};
    OBL_CHECK(runtime.RunningMods() == left);
    OBL_REQUIRE(runtime.Failures().size() == 4u);
    for (const std::string& failure : runtime.Failures()) OBL_CHECK(Contains(failure, "instruction budget"));
    OBL_CHECK(Contains(runtime.Failures()[1], "scripts/presentation/main.lua:2:"));
}

OBL_TEST_CASE("script_runtime.the_memory_cap_switches_off_a_greedy_mod")
{
    test::TempPath game("scripts-memory");
    mods::ScriptLimits limits;
    limits.memoryPerMod = 1u << 20;
    limits.instructionsPerCall = 1u << 30;
    const std::vector<mods::ScriptMod> list = {
            Mod(game.path, "greedy", "local t = {}\nfunction on_start() for i = 1, 1e7 do t[i] = string.rep('x', 64) .. i end end"),
            Mod(game.path, "rep", "function on_start() local s = string.rep('x', 1 << 24) end"),
            Mod(game.path, "fine", "local t = {}\nfunction on_start() for i = 1, 100 do t[i] = i end end")};
    FakeHost host;
    mods::ScriptRuntime runtime(host, limits);
    runtime.Start(list);
    const std::vector<std::string> left = {"fine"};
    OBL_CHECK(runtime.RunningMods() == left);
    OBL_REQUIRE(runtime.Failures().size() == 2u);
    for (const std::string& failure : runtime.Failures()) OBL_CHECK(Contains(failure, "memory cap"));
}

OBL_TEST_CASE("script_runtime.the_sandbox_has_no_files_processes_clock_or_code_loading")
{
    test::TempPath game("scripts-sandbox");
    const char* escapes[] = {"io.open('x')", "os.time()", "os.execute('x')", "require('x')", "package.loadlib('x', 'y')",
            "load('return 1')", "loadfile('x')", "dofile('x')", "string.dump(print)", "debug.getinfo(1)", "collectgarbage()",
            "math.randomseed(1)", "loadstring('return 1')"};
    std::vector<mods::ScriptMod> list;
    for (size_t i = 0; i < std::size(escapes); ++i)
        list.push_back(Mod(game.path, "escape" + std::to_string(i), std::string("function on_start() ") + escapes[i] + " end"));
    // What a script does need is there.
    list.push_back(Mod(game.path, "fine",
            "function on_start() print(string.format('%d', math.floor(2.5)), table.concat({'a', 'b'}), utf8.char(65), "
            "coroutine.wrap(function() return 'co' end)(), type(math.random()), obl.api) end"));
    FakeHost host;
    mods::ScriptRuntime runtime(host);
    runtime.Start(list);
    const std::vector<std::string> left = {"fine"};
    OBL_CHECK(runtime.RunningMods() == left);
    OBL_CHECK_EQ(runtime.Failures().size(), std::size(escapes));
    for (const std::string& failure : runtime.Failures()) OBL_CHECK(Contains(failure, "nil value"));
    OBL_CHECK(host.Logged("mod fine: 2\tab\tA\tco\tnumber\t1"));
}

OBL_TEST_CASE("script_runtime.a_precompiled_chunk_is_refused")
{
    test::TempPath game("scripts-binary");
    FakeHost host;
    mods::ScriptRuntime runtime(host);
    runtime.Start(std::vector<mods::ScriptMod>{Mod(game.path, "binary", std::string("\x1bLua\x54\x00", 6) + "junk")});
    OBL_CHECK(runtime.RunningMods().empty());
    OBL_REQUIRE(runtime.Failures().size() == 1u);
    OBL_CHECK(Contains(runtime.Failures()[0], "binary"));
}

OBL_TEST_CASE("script_runtime.commands_off_the_list_are_refused")
{
    test::TempPath game("scripts-commands");
    const std::vector<mods::ScriptMod> list = {
            Mod(game.path, "speed",
                    "function on_frame() if obl.game.in_world() then obl.command('set-game-speed', { level = 2.0 }) end end"),
            Mod(game.path, "pause", "function on_start() obl.command('set-paused', { paused = true }) obl.command('resume-game') end"),
            Mod(game.path, "place",
                    "function on_start() obl.command('place-building', { key = 'Beach Bar', x = 40, y = 52, dir = 2 }) end"),
            Mod(game.path, "hire", "function on_start() obl.command('hire-staff', { profession = 'Cleaner' }) end"),
            Mod(game.path, "missing", "function on_start() obl.command('set-game-speed', {}) end"),
            Mod(game.path, "unknown", "function on_start() obl.command('set-game-speed', { level = 1, speed = 2 }) end")};
    FakeHost host;
    host.inWorld = false;
    mods::ScriptRuntime runtime(host);
    runtime.Start(list);
    runtime.Frame(0.02f);
    host.inWorld = true;
    runtime.Frame(0.02f);
    const std::vector<std::string> sent = {
            "set-paused paused=1", "resume-game", "place-building key=Beach%20Bar x=40 y=52 dir=2", "set-game-speed level=2"};
    OBL_CHECK(host.commands == sent);
    const std::vector<std::string> left = {"speed", "pause", "place"};
    OBL_CHECK(runtime.RunningMods() == left);
    OBL_REQUIRE(runtime.Failures().size() == 3u);
    OBL_CHECK(Contains(runtime.Failures()[0], "\"hire-staff\" is not a command open to scripts"));
    OBL_CHECK(Contains(runtime.Failures()[1], "\"missing\""));
    OBL_CHECK(Contains(runtime.Failures()[2], "speed"));
}

OBL_TEST_CASE("script_runtime.the_store_outlives_a_run_and_has_a_cap")
{
    test::TempPath game("scripts-store");
    const auto mod = Mod(game.path, "keeper", R"(
        function on_start()
          local runs = obl.store.get("runs") or 0
          print("runs " .. runs .. " " .. tostring(obl.store.get("name")) .. " " .. tostring(obl.store.get("flag")))
          obl.store.set("runs", runs + 1)
          obl.store.set("name", "beach")
          obl.store.set("flag", true)
          obl.store.set("ratio", 0.25)
          obl.store.set("ratio", nil)
        end)");
    FakeHost host;
    for (int run = 0; run < 2; ++run) {
        mods::ScriptRuntime runtime(host);
        runtime.Start(std::vector<mods::ScriptMod>{mod});
    }
    OBL_CHECK(host.Logged("mod keeper: runs 0 nil nil"));
    OBL_CHECK(host.Logged("mod keeper: runs 1 beach true"));
    OBL_CHECK(Contains(ReadText(mod.storeFile), "\"obl.mod_store\""));
    OBL_CHECK(!Contains(ReadText(mod.storeFile), "ratio"));

    mods::ScriptLimits limits;
    limits.storeBytes = 100;
    mods::ScriptRuntime small(host, limits);
    small.Start(std::vector<mods::ScriptMod>{Mod(game.path, "big", "function on_start() obl.store.set('k', string.rep('x', 200)) end"),
            Mod(game.path, "table", "function on_start() obl.store.set('k', {}) end")});
    OBL_REQUIRE(small.Failures().size() == 2u);
    OBL_CHECK(Contains(small.Failures()[0], "exceed 100 bytes"));
    OBL_CHECK(Contains(small.Failures()[1], "a value is nil, a boolean, a number or a string"));
}

namespace {

settings_schema::Field Setting(const std::string& id, settings_schema::Kind kind, const std::string& defaultValue)
{
    settings_schema::Field field;
    field.id = id;
    field.kind = kind;
    field.label = id;
    field.defaultValue = defaultValue;
    return field;
}

std::vector<settings_schema::Field> SampleSettings()
{
    auto theme = Setting("theme", settings_schema::Kind::Choice, "sand");
    theme.options = {{"sand", "Sand"}, {"sea", "Sea"}};
    auto zoom = Setting("zoom", settings_schema::Kind::Number, "2");
    zoom.min = 1;
    zoom.max = 4;
    zoom.step = 1;
    auto opacity = Setting("opacity", settings_schema::Kind::Number, "0.5");
    opacity.min = 0;
    opacity.max = 1;
    opacity.step = 0.25;
    return {Setting("wide", settings_schema::Kind::Toggle, "0"), theme, zoom, opacity};
}

} // namespace

OBL_TEST_CASE("script_runtime.settings_are_read_typed_with_the_default_for_a_bad_value")
{
    test::TempPath game("scripts-settings");
    auto mod = Mod(game.path, "m", R"(
        function on_start()
          print(type(obl.settings.get("wide")), tostring(obl.settings.get("wide")))
          print(obl.settings.get("theme"))
          print(math.type(obl.settings.get("zoom")), obl.settings.get("zoom"))
          print(math.type(obl.settings.get("opacity")), obl.settings.get("opacity"))
          print(pcall(obl.settings.get, "nope"))
        end)");
    mod.settings = SampleSettings();
    FakeHost host;
    host.settings = {{"m.wide", "true"}, {"m.theme", "purple"}, {"m.zoom", "3"}};
    mods::ScriptRuntime runtime(host);
    runtime.Start(std::vector<mods::ScriptMod>{mod});
    OBL_CHECK(host.Logged("mod m: boolean\ttrue"));
    OBL_CHECK(host.Logged("mod m: sand")); // "purple" is not an option: the default
    OBL_CHECK(host.Logged("mod m: integer\t3"));
    OBL_CHECK(host.Logged("mod m: float\t0.5"));
    OBL_CHECK(host.Logged("mod m: the setting theme holds \"purple\", which is not a value of it; using the default"));
    bool refused = false;
    for (const std::string& line : host.lines) refused |= Contains(line, "\"nope\" is not a setting this mod declares");
    OBL_CHECK(refused);
}

OBL_TEST_CASE("script_runtime.on_setting_runs_once_for_each_changed_value")
{
    test::TempPath game("scripts-setting-change");
    auto mod = Mod(game.path, "m", R"(
        function on_setting(id, value) print("changed " .. id .. " " .. tostring(value)) end)");
    mod.settings = SampleSettings();
    auto other = Mod(game.path, "other", "function on_setting(id, value) print('never') end");
    FakeHost host;
    mods::ScriptRuntime runtime(host);
    runtime.Start(std::vector<mods::ScriptMod>{mod, other});
    runtime.SettingsChanged(); // nothing changed since Start
    OBL_CHECK(!Contains(host.lines.empty() ? "" : host.lines.back(), "changed"));
    host.settings = {{"m.wide", "1"}, {"m.zoom", "4"}, {"m.theme", "sea"}};
    runtime.SettingsChanged();
    runtime.SettingsChanged(); // read again: no new change
    std::vector<std::string> changes;
    for (const std::string& line : host.lines)
        if (Contains(line, "changed ")) changes.push_back(line);
    const std::vector<std::string> expected = {"mod m: changed wide true", "mod m: changed theme sea", "mod m: changed zoom 4"};
    OBL_CHECK(changes == expected);
    OBL_CHECK(!host.Logged("mod other: never"));
    // A value that becomes invalid falls back to the default, which is a change from what the script saw.
    host.settings = {{"m.wide", "1"}, {"m.zoom", "9"}, {"m.theme", "sea"}};
    runtime.SettingsChanged();
    OBL_CHECK(host.Logged("mod m: changed zoom 2"));
}

OBL_TEST_CASE("script_runtime.mod_settings_live_in_their_own_section_of_openbeach_ini")
{
    test::TempPath game("scripts-setting-ini");
    const auto file = game.path / "openbeach.ini";
    Write(file, "[Display]\r\nWindowed=0\r\nScale=2\r\n\r\n[Mod.m]\r\nzoom = 3 \r\nempty=\r\n");
    OBL_CHECK_EQ(mods::ReadModSettingFile(file, "m", "zoom").value_or("?"), std::string("3"));
    OBL_CHECK_EQ(mods::ReadModSettingFile(file, "m", "empty").value_or("?"), std::string()); // present but empty
    OBL_CHECK(!mods::ReadModSettingFile(file, "m", "missing"));
    OBL_CHECK(!mods::ReadModSettingFile(file, "other", "zoom")); // another mod's section
    OBL_CHECK(!mods::ReadModSettingFile(game.path / "none.ini", "m", "zoom"));

    const game::start_settings::Values before = game::start_settings::Load(game.path, game.path);
    OBL_CHECK(mods::WriteModSettingFile(file, "m", "theme", "sea"));
    OBL_CHECK(mods::WriteModSettingFile(file, "m", "zoom", "4"));
    OBL_CHECK_EQ(mods::ReadModSettingFile(file, "m", "theme").value_or("?"), std::string("sea"));
    OBL_CHECK_EQ(mods::ReadModSettingFile(file, "m", "zoom").value_or("?"), std::string("4"));
    // The engine's own keys read as before: a mod's section is no setting of the game.
    OBL_CHECK(game::start_settings::Load(game.path, game.path) == before);
    OBL_CHECK(!before.windowed);
    OBL_CHECK_EQ(before.scale, 2.0);
}

OBL_TEST_CASE("script_runtime.a_failing_on_setting_switches_off_only_its_mod")
{
    test::TempPath game("scripts-setting-error");
    auto bad = Mod(game.path, "bad", "function on_setting(id, value) error('boom') end");
    bad.settings = SampleSettings();
    auto good = Mod(game.path, "good", "function on_setting(id, value) print('ok ' .. id) end");
    good.settings = SampleSettings();
    FakeHost host;
    mods::ScriptRuntime runtime(host);
    runtime.Start(std::vector<mods::ScriptMod>{bad, good});
    host.settings = {{"bad.wide", "1"}, {"good.wide", "1"}};
    runtime.SettingsChanged();
    const std::vector<std::string> running = {"good"};
    OBL_CHECK(runtime.RunningMods() == running);
    OBL_CHECK(host.Logged("mod good: ok wide"));
}

OBL_TEST_CASE("script_runtime.the_reference_page_lists_the_registered_api_and_commands")
{
    const std::string page = ReadText(std::filesystem::path(OBL_REPO_DIR) / "docs/modding/reference/scripting.md");
    OBL_REQUIRE(!page.empty());
    const std::vector<std::string> registered = mods::RegisteredApi();
    OBL_CHECK(!registered.empty());
    const std::vector<std::string> listed = ListedNames(page, "## API", std::regex(R"(^\| `(obl\.[a-z_.]+))"));
    OBL_CHECK(listed == registered);
    std::vector<std::string> commands(mods::ScriptCommands().begin(), mods::ScriptCommands().end());
    std::sort(commands.begin(), commands.end());
    OBL_CHECK(ListedNames(page, "## Commands", std::regex(R"(^\| `([a-z-]+)`)")) == commands);
}

OBL_TEST_CASE("script_runtime.every_key_code_has_its_name")
{
    const std::string header = ReadText(std::filesystem::path(OBL_REPO_DIR) / "src/core/key_code.h");
    const std::regex row(R"(^\s+(\w+) = 0x([0-9a-f]+),)");
    int count = 0;
    std::istringstream lines(header);
    for (std::string line; std::getline(lines, line);) {
        std::smatch match;
        if (!std::regex_search(line, match, row) || match[1] == "None") continue;
        ++count;
        OBL_CHECK_EQ(std::string(mods::KeyName(static_cast<uint8_t>(std::stoi(match[2], nullptr, 16)))), std::string(match[1]));
    }
    OBL_CHECK(count > 100);
    OBL_CHECK(mods::KeyName(0).empty());
}
