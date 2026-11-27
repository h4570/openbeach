// The converted GUI layouts as the GUI engine reads them. The layouts table keeps its file names for the life of the game, so the
// names FileFor returns stay valid.

#include "core/char16.h"
#include "core/pack_path.h"
#include "gui_layouts_runtime.h"

#include "game/game/diagnostics.h"
#include "game/ui/gui_layouts.h"
#include "game/ui/gui_patch.h"
#include "game/ui/extra_scenarios_screen.h"
#include "game/ui/own_layer.h"
#include "core/log.h"
#include "core/wide_path.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace obl::gui::gui_layouts {

namespace {

obl::gui_layouts::Layouts g_layouts;
std::filesystem::path g_pack;
std::string g_error = "not loaded";
bool g_loaded = false;
std::unordered_map<std::string, std::string> g_fileLayout;

// Headless runs never reach the GUI, so they start without the files; a windowed game stops with the reason.
[[noreturn]] void Require(const std::string& reason)
{
    game::FatalError("Cannot load remaster GUI layouts (%s).\r\nImport the game data again", reason.c_str());
    std::abort();
}

} // namespace

bool Load(const Text16& gameDirectory)
{
    if (g_loaded) return true;
    g_pack = obl::WidePath(gameDirectory) / obl::pack_path::Selected();
    if (!obl::gui_layouts::LoadLayouts(g_pack, g_layouts, g_error)) {
        OBL_LOG("gui layouts: %s", g_error.c_str());
        return false;
    }
    g_loaded = true;
    g_error.clear();
    OBL_LOG("gui layouts: %u layouts", static_cast<unsigned>(g_layouts.size()));
    return true;
}

void Reload()
{
    if (!obl::gui_layouts::LoadLayouts(g_pack, g_layouts, g_error)) Require(g_error);
    g_fileLayout.clear();
}

const char* FileFor(const char* layout, int language)
{
    if (!layout) return nullptr;
    if (!g_loaded) Require(g_error);
    const obl::gui_layouts::Choice* choice = obl::gui_layouts::Resolve(g_layouts, layout, language);
    if (!choice) Require(std::string("no file for layout ") + layout);
    g_fileLayout[choice->file] = layout;
    return choice->file.c_str();
}

std::string ReadScript(const char* file)
{
    if (!g_loaded) Require(g_error);
    const obl::gui_layouts::Choice* choice = obl::gui_layouts::FindFile(g_layouts, file);
    if (!choice) Require(std::string("no layout for file ") + file);
    std::string bytes, error;
    if (!obl::gui_layouts::ReadText(g_pack, *choice, bytes, error)) Require(error);
    return bytes;
}

std::string ReadPatchedScript(const char* file, int screenWidth, int screenHeight, std::vector<gui_patch::ControlBinding>& bindings)
{
    bindings.clear();
    std::string base = ReadScript(file);
    if (!file) return base;
    std::string layout;
    if (const auto it = g_fileLayout.find(file); it != g_fileLayout.end()) {
        layout = it->second;
    } else {
        for (const auto& [key, choices] : g_layouts) {
            for (const auto& [lang, choice] : choices) {
                if (choice.file == file) {
                    layout = key;
                    break;
                }
            }
            if (!layout.empty()) break;
        }
    }
    if (layout.empty()) return base;
    const auto own = obl::own_layer::GuiPatches(layout, obl::gui::extra_scenarios_screen::HasExtraScenarios());
    const auto patchFiles = obl::gui_patch::FindPatchFiles(g_pack, layout);
    if (patchFiles.empty() && own.empty()) return base;
    obl::gui_patch::ApplyResult result;
    std::string error;
    const auto& mods = obl::pack_path::ActiveMods();
    if (!obl::gui_patch::ApplyPackPatches(g_pack, layout, base, mods, screenWidth, screenHeight, result, error, own)) {
        if (own.empty()) Require(error);
        // A pack whose menu the own layer does not fit (no MainMenu dialog, a dialog of the same name) still starts, without the
        // mods screen; a mod's own error stops the game as before.
        OBL_LOG("own layer: %s; %s loads without it", error.c_str(), layout.c_str());
        if (patchFiles.empty()) return base;
        if (!obl::gui_patch::ApplyPackPatches(g_pack, layout, base, mods, screenWidth, screenHeight, result, error)) Require(error);
    }
    bindings = std::move(result.bindings);
    return result.scriptText;
}

} // namespace obl::gui::gui_layouts
