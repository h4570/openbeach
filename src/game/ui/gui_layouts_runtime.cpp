// The converted GUI layouts as the GUI engine reads them. The layouts table keeps its file names for the life of the game, so the
// names FileFor returns stay valid.

#include "core/char16.h"
#include "core/pack_path.h"
#include "gui_layouts_runtime.h"

#include "game/game/diagnostics.h"
#include "game/ui/gui_layouts.h"
#include "core/log.h"
#include "core/wide_path.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace obl::gui::gui_layouts {

namespace {

obl::gui_layouts::Layouts g_layouts;
std::filesystem::path g_pack;
std::string g_error = "not loaded";
bool g_loaded = false;

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

const char* FileFor(const char* layout, int language)
{
    if (!layout) return nullptr;
    if (!g_loaded) Require(g_error);
    const obl::gui_layouts::Choice* choice = obl::gui_layouts::Resolve(g_layouts, layout, language);
    if (!choice) Require(std::string("no file for layout ") + layout);
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

} // namespace obl::gui::gui_layouts
