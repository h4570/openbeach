#include "game/mods/dev_mode.h"

#include "core/config.h"
#include "core/log.h"
#include "core/pack_path.h"
#include "core/sha256.h"
#include "core/wide_path.h"
#include "formats/gui_content.h"
#include "formats/mod_pack.h"
#include "game/feedback/feedback_manager.h"
#include "game/mods/presentation_scripts.h"
#include "game/objects/script_program_load.h"
#include "game/tables/text_load.h"
#include "game/ui/gui_layouts.h"
#include "game/ui/gui_patch.h"
#include "game/ui/own_layer.h"
#include "game/world/world_script_load.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <utility>

namespace obl::mods::dev {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using mod_pack::Scope;

struct Copy {
    fs::path directory;
    fs::path startupDirectory; // some host resources keep their original paths for the session
    std::string seen;
};

Text16 g_gameDirectory;
fs::path g_root;
std::map<std::string, Copy> g_copies;
unsigned g_generation = 0;
Clock::time_point g_nextPoll;
std::string g_error;

void SetError(std::string error)
{
    if (!error.empty() && error != g_error) OBL_LOG("mod dev: %s", error.c_str());
    g_error = std::move(error);
}

void RemoveCopy(const fs::path& directory)
{
    if (directory.empty()) return;
    std::error_code ignored;
    fs::remove_all(directory.parent_path(), ignored);
}

bool ValidatePresentation(const fs::path& pack, const Changes& changes, std::string& error)
{
    if (changes.texts) {
        std::vector<text::Entry> entries;
        for (unsigned language = 0; language < text_load::kLanguageCount; ++language)
            if (!text_load::LoadPack(pack, text_load::LanguageCode(language), entries, error)) return false;
    }
    if (!changes.gui) return true;
    gui_content::Content content;
    if (!gui_content::LoadPack(pack, content, error)) return false;
    gui_layouts::Layouts layouts;
    if (!gui_layouts::LoadLayouts(pack, layouts, error)) return false;
    for (const auto& [layout, choices] : layouts) {
        if (gui_patch::FindPatchFiles(pack, layout).empty()) continue;
        for (const auto& [language, choice] : choices) {
            std::string bytes;
            if (!gui_layouts::ReadText(pack, choice, bytes, error)) return false;
            gui_patch::ApplyResult result;
            const auto& ids = pack_path::ActiveMods();
            auto leading = own_layer::GuiPatches(layout);
            if (!leading.empty() &&
                    !gui_patch::ApplyPatches(bytes, leading, ids, gui_patch::kReferenceWidth, gui_patch::kReferenceHeight, result, error))
                leading.clear();
            if (!gui_patch::ApplyPackPatches(
                        pack, layout, bytes, ids, gui_patch::kReferenceWidth, gui_patch::kReferenceHeight, result, error, leading))
                return false;
        }
    }
    return true;
}

bool LoadWorldScripts(std::string& error)
{
    std::vector<std::string> errors;
    const bool valid = object::LoadScriptProgramVocabulary(g_gameDirectory, &errors) && world::LoadWorldScriptVocabulary(g_gameDirectory) &&
            world::CheckModWorldScripts(&errors) && feedback::LoadFeedbackVocabulary(g_gameDirectory);
    if (!valid) error = errors.empty() ? "could not load the world mod vocabulary" : errors.front();
    return valid;
}

Changes Refresh(Scope scope, bool startup)
{
    const fs::path gameDirectory = WidePath(g_gameDirectory);
    const std::string packId = pack_path::SelectedId();
    const std::vector<std::string> ids = pack_path::ActiveMods();
    std::map<std::string, Copy> staged;
    Changes changes;
    std::string error;
    for (const auto& mod : mod_pack::Active()) {
        if (!startup && mod.manifest.scope != scope) continue;
        const auto& id = mod.manifest.id;
        auto& copy = g_copies[id];
        const auto source = gameDirectory / "mods" / id;
        const auto hash = pack_path::ModContentHash(source);
        if (!startup && hash == copy.seen && !copy.directory.empty()) continue;
        const auto destination = g_root / std::to_string(++g_generation) / id;
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        if (!ec) fs::copy(source, destination, fs::copy_options::recursive, ec);
        if (ec || hash.empty() || pack_path::ModContentHash(destination) != hash) {
            RemoveCopy(destination);
            error = "mod \"" + id + "\": could not read a stable copy of the files";
            break;
        }
        if (scope == Scope::World && !copy.directory.empty()) {
            // These shared banks are constructed before the first world. Keeping their source files frozen keeps the
            // recorded mod hash in agreement with the resources each world actually uses.
            const auto changed = [&](const fs::path& directory) {
                return pack_path::ModContentHash(copy.directory / directory) != pack_path::ModContentHash(destination / directory);
            };
            const auto rulesChanged = [&](const char* file) {
                std::error_code ignored;
                const auto before = copy.directory / file;
                const auto after = destination / file;
                if (!fs::exists(before, ignored) && !fs::exists(after, ignored)) return false;
                return sha256::OfFile(before) != sha256::OfFile(after);
            };
            if (changed("sprites") || changed("sounds") || rulesChanged("rules/game_parameters.json") ||
                    rulesChanged("rules/game_parameters.patch.json") || rulesChanged("scenarios/campaign.json") ||
                    rulesChanged("scenarios/campaign.patch.json")) {
                copy.seen = hash;
                RemoveCopy(destination);
                error = "mod \"" + id + "\": changes to shared resources require a restart";
                break;
            }
        }
        if (!startup) {
            const auto changed = [&](const char* dir) {
                const auto oldDir = (copy.directory.empty() ? source : copy.directory) / dir;
                return pack_path::ModContentHash(oldDir) != pack_path::ModContentHash(destination / dir);
            };
            changes.texts |= changed("text");
            changes.gui |= changed("gui");
        }
        staged.emplace(id, Copy{destination, {}, hash});
    }
    if (!error.empty()) {
        for (const auto& [id, candidate] : staged) RemoveCopy(candidate.directory);
        SetError(std::move(error));
        return {};
    }
    if (staged.empty()) {
        return {};
    }
    const auto previous = mod_pack::Active();
    if (error.empty()) {
        for (const auto& [id, candidate] : staged) {
            g_copies.at(id).seen = candidate.seen;
            pack_path::SetModDirectoryOverride(id, candidate.directory);
        }
        if (mod_pack::ActivateList(gameDirectory, packId, ids, error)) {
            const auto& active = mod_pack::Active();
            for (size_t i = 0; i < active.size(); ++i)
                if (active[i].manifest.scope != previous[i].manifest.scope) {
                    error = "mod \"" + ids[i] + "\": changing scope requires a restart";
                    break;
                }
            if (error.empty() && !startup && ValidatePresentation(gameDirectory / pack_path::Selected(), changes, error) &&
                    scope == Scope::World)
                LoadWorldScripts(error);
        }
    }
    if (!error.empty()) {
        for (const auto& [id, candidate] : staged) {
            pack_path::SetModDirectoryOverride(id, g_copies.at(id).directory);
            RemoveCopy(candidate.directory);
        }
        std::string restoreError;
        if (mod_pack::ActivateList(gameDirectory, packId, ids, restoreError) && !startup && scope == Scope::World)
            LoadWorldScripts(restoreError);
        SetError(std::move(error));
        return {};
    }
    for (auto& [id, candidate] : staged) {
        auto& copy = g_copies.at(id);
        if (copy.directory != copy.startupDirectory) RemoveCopy(copy.directory);
        copy.directory = std::move(candidate.directory);
        if (startup) copy.startupDirectory = copy.directory;
        if (!startup) OBL_LOG("mod dev: reloaded %s", id.c_str());
    }
    if (!startup) RestartPresentationScripts();
    SetError({});
    return changes;
}

} // namespace

void Start(const Text16& gameDirectory)
{
    if (!config::Get().modDev || config::Get().headless) return;
    g_gameDirectory = gameDirectory;
    const auto mods = WidePath(gameDirectory) / "mods";
    std::error_code ec;
    fs::create_directories(mods, ec);
    const auto stamp = Clock::now().time_since_epoch().count();
    const auto root = mods / (".dev-" + std::to_string(stamp));
    if (!ec && fs::create_directory(root, ec)) {
        g_root = root;
        Refresh(Scope::Presentation, true);
    } else {
        SetError("cannot create a workspace in the mods directory");
    }
}

void Stop()
{
    for (const auto& [id, copy] : g_copies) pack_path::SetModDirectoryOverride(id, {});
    g_copies.clear();
    std::error_code ignored;
    if (!g_root.empty()) fs::remove_all(g_root, ignored);
    g_root.clear();
}

Changes Poll()
{
    if (g_root.empty() || Clock::now() < g_nextPoll) return {};
    g_nextPoll = Clock::now() + std::chrono::milliseconds(500);
    for (auto it = g_copies.begin(); it != g_copies.end();) {
        const auto& ids = pack_path::ActiveMods();
        if (std::find(ids.begin(), ids.end(), it->first) != ids.end()) {
            ++it;
            continue;
        }
        pack_path::SetModDirectoryOverride(it->first, {});
        RemoveCopy(it->second.directory);
        it = g_copies.erase(it);
    }
    return Refresh(Scope::Presentation, false);
}

Changes BeforeNewGame()
{
    if (g_root.empty()) return {};
    return Refresh(Scope::World, false);
}

bool Enabled()
{
    return config::Get().modDev && !config::Get().headless;
}

std::string_view Error()
{
    return g_error;
}

} // namespace obl::mods::dev
