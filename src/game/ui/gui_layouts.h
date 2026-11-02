#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace obl::gui_layouts {

// The file a layout uses for one language. `polish`: the file is Windows-1250 (the "pl" choice); all others are Windows-1252.
struct Choice {
    std::string file;
    bool polish = false;
};

// Layout key (`main_menu`, `in_game`, ...) -> language code (`fr`, `pl`, ..., `*` = fallback) -> choice.
using Layouts = std::map<std::string, std::map<std::string, Choice, std::less<>>, std::less<>>;

// Reads and validates <pack>/gui/layouts.json (obl.gui_layouts). Transactional: `out` is untouched on failure.
bool LoadLayouts(const std::filesystem::path& pack, Layouts& out, std::string& error);

// The language code of the engine's language number (game_states.h: 1 fr, 2 it, 3 de, 4 es, 5 pl); English and any other number
// use the fallback "*".
std::string_view LanguageCode(int language);

// The layout's choice for a language number: the language's own file, else the "*" fallback. nullptr for an unknown layout.
const Choice* Resolve(const Layouts& layouts, std::string_view layout, int language);

// The choice whose file has this name (`main_menu_fr.gui`); nullptr when no layout uses the file.
const Choice* FindFile(const Layouts& layouts, std::string_view file);

bool ReadText(const std::filesystem::path& pack, const Choice& choice, std::string& bytes, std::string& error);

} // namespace obl::gui_layouts
