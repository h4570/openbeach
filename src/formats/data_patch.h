#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <span>
#include <string>
#include <vector>

namespace obl::data_patch {

// Opens a resolved pack file for a loader: the patched copy Prepare built for it, else the file itself, so with no
// patches every loader reads the same bytes as before.
class Input : public std::istream {
public:
    explicit Input(const std::filesystem::path& file);

private:
    std::filebuf m_file;
    std::stringbuf m_text;
};

// Whether a logical pack path (`rules/game_parameters.json`) feeds the world. Patching such a file makes a mod a world
// mod; the start-up scope check of presentation mods uses this list.
bool IsSimulationDataPath(const std::filesystem::path& logical);

// The `format` of a patch file, empty when it cannot be read or names none. `gui/<layout>.patch.json` is a JSON data patch
// (`obl.json_patch`) when it patches a JSON file such as `gui/object_browser.json`, and a GUI layout patch (`obl.gui_patch`,
// game/ui/gui_patch.h) when it patches a `.gui` layout: the two loaders tell them apart by this field, not by the path.
std::string PatchFormat(const std::filesystem::path& file);
inline constexpr const char* kGuiPatchFormat = "obl.gui_patch";

// A value a later mod sets over an earlier one's: the later mod wins (docs/modding/reference/data-patches.md).
struct Conflict {
    std::string winner, loser; // mod ids
    std::string file; // logical path of the patched file
    std::string path; // JSON pointer of the value
    std::string text; // "<winner, file, op, path> overrides <loser, ...>", the start-up log's line
};

// Applies the `<logical>.patch.json` files of `mods` (load order) to the files they name and checks each result against
// its schema. All documents are built before any is published, so a failure keeps the previous state. `conflicts` lists
// each value a later mod sets over an earlier one's, naming both; `error` names the mod, the file, the op and the path.
bool Prepare(const std::filesystem::path& pack, std::span<const std::string> mods, std::vector<std::string>& conflicts, std::string& error);

// What Prepare would find for `mods`, without publishing anything: the mods screen lists the conflicts of a list it has not applied.
bool FindConflicts(
        const std::filesystem::path& pack, std::span<const std::string> mods, std::vector<Conflict>& conflicts, std::string& error);

} // namespace obl::data_patch
