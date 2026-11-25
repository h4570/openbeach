#include "game/mods/presentation_scripts.h"

#include "core/ini_file.h"
#include "core/log.h"
#include "core/os.h"
#include "formats/mod_pack.h"
#include "game/game/command_loop.h"
#include "game/game/start_settings.h"
#include "game/mods/mod_settings_file.h"
#include "game/mods/key_names.h"
#include "game/mods/script_runtime.h"
#include "game/ui/input_types.h"

#include <algorithm>
#include <array>
#include <optional>
#include <map>
#include <memory>

namespace obl::mods {

namespace {

// The settings the player's openbeach.ini holds for mods are `[Mod.<id>] <key>`. A scripted test run leaves the player's file alone,
// so what SetModSetting sets there stays in memory and is read first.
std::filesystem::path g_settingsFile;
std::map<std::string, std::string> g_testSettings; // "<mod>.<id>" -> value

class GameScriptHost : public ScriptHost {
public:
    void Log(const std::string& line) override { OBL_LOG("%s", line.c_str()); }
    void SubmitCommand(command::Command command) override { game::SubmitCommand(std::move(command)); }
    bool InWorld() const override { return game::GetCommandLoopStats().inWorld; }
    std::optional<std::string> ReadSetting(const std::string& mod, const std::string& key) const override
    {
        if (const auto it = g_testSettings.find(mod + "." + key); it != g_testSettings.end()) return it->second;
        if (g_settingsFile.empty()) return std::nullopt;
        return ReadModSettingFile(g_settingsFile, mod, key);
    }
};

GameScriptHost g_host;
std::unique_ptr<ScriptRuntime> g_runtime;
bool g_started = false;
// The input of the previous frame, to turn the state the input object holds into down and up events.
std::array<uint8_t, 256> g_keys{};
bool g_left = false, g_right = false;

void Start()
{
    g_started = true;
    std::vector<ScriptMod> mods;
    for (const mod_pack::ActiveMod& mod : mod_pack::Active()) {
        // The store sits beside mods/, outside saves and outside the mod's directory, so updating the mod keeps it.
        const std::filesystem::path gameDirectory = mod.directory.parent_path().parent_path();
        g_settingsFile = gameDirectory / game::start_settings::kFileName;
        mods.push_back({mod.manifest.id, mod.directory, gameDirectory / "mod-data" / (mod.manifest.id + ".json"), mod.manifest.settings});
    }
    if (mods.empty()) return;
    g_runtime = std::make_unique<ScriptRuntime>(g_host);
    g_runtime->Start(mods);
}

void DispatchInput(const ui::Input& input)
{
    for (int code = 1; code < 256; ++code) {
        const uint8_t now = input.m_keys[code];
        const std::string_view name = KeyName(static_cast<uint8_t>(code));
        // 1 is "pressed in this update"; a key that was down and reads 0 now was released.
        if (!name.empty() && (now == 1 || (now == 0 && g_keys[code] != 0)))
            g_runtime->Input({"key", now ? "down" : "up", std::string(name), "", 0, 0});
        g_keys[code] = now;
    }
    const auto button = [&](bool now, bool& before, const char* name) {
        if (now != before) g_runtime->Input({"mouse", now ? "down" : "up", "", name, input.m_x, input.m_y});
        before = now;
    };
    button(input.m_leftButton != 0, g_left, "left");
    button(input.m_rightButton != 0, g_right, "right");
}

} // namespace

void PresentationFrame(float dt, const ui::Input* input)
{
    if (!g_started) Start();
    if (!g_runtime) return;
    if (input) DispatchInput(*input);
    g_runtime->Frame(dt);
}

bool SetModSetting(const std::string& mod, const std::string& id, const std::string& value, std::string& error)
{
    const auto& active = mod_pack::Active();
    const auto found = std::find_if(active.begin(), active.end(), [&](const auto& m) { return m.manifest.id == mod; });
    if (found == active.end()) {
        error = "mod \"" + mod + "\" is not active";
        return false;
    }
    const auto& fields = found->manifest.settings;
    const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return f.id == id; });
    if (field == fields.end()) {
        error = "mod \"" + mod + "\" declares no setting \"" + id + "\"";
        return false;
    }
    const auto canonical = settings_schema::Canonical(*field, value);
    if (!canonical) {
        error = "\"" + value + "\" is not a value of the setting \"" + id + "\" of mod \"" + mod + "\"";
        return false;
    }
    if (os::HasEnv("OBL_SCRIPT")) {
        g_testSettings[mod + "." + id] = *canonical;
    } else {
        const std::filesystem::path file = found->directory.parent_path().parent_path() / game::start_settings::kFileName;
        g_settingsFile = file;
        game::start_settings::CreateDefaultFile(file);
        if (!WriteModSettingFile(file, mod, id, *canonical)) {
            error = "cannot write " + file.generic_string();
            return false;
        }
    }
    if (g_runtime) g_runtime->SettingsChanged();
    return true;
}

void StopPresentationScripts()
{
    if (g_runtime) g_runtime->Stop();
    g_runtime.reset();
}

std::vector<std::string> RunningPresentationMods()
{
    return g_runtime ? g_runtime->RunningMods() : std::vector<std::string>{};
}

std::vector<std::string> PresentationFailures()
{
    return g_runtime ? g_runtime->Failures() : std::vector<std::string>{};
}

} // namespace obl::mods
