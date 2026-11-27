#include "app/init.h"

#include "core/config.h"
#include "core/json_schema.h"
#include "core/log.h"
#include "core/ini_file.h"
#include "game/game/start_settings.h"
#include "core/pack_path.h"
#include "core/png_read.h"
#include "formats/asset_recipe.h"
#include "formats/data_patch.h"
#include "formats/gui_content.h"
#include "formats/mod_pack.h"
#include "formats/schemas.h"
#include "formats/sprite_pack.h"
#include "game/mods/mod_settings_file.h"
#include "game/mods/script_runtime.h"
#include "game/objects/script_program_load.h"
#include "game/tables/catalog_load.h"
#include "game/tables/guest_set_load.h"
#include "game/tables/text_load.h"
#include "game/ui/gui_layouts.h"
#include "game/ui/gui_patch.h"
#include "game/ui/own_layer.h"
#include "game/world/scenario_load.h"
#include "game/world/scenario_registry.h"
#include "game/world/world_script_load.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>

namespace obl {
namespace {
namespace fs = std::filesystem;
using nlohmann::json;
using Messages = std::vector<std::string>;

bool IsFile(const fs::path& file)
{
    std::error_code error;
    return fs::is_regular_file(file, error);
}

std::string Lower(std::string value)
{
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

class CheckHost : public mods::ScriptHost {
public:
    explicit CheckHost(const fs::path& ini) : m_ini(ini) {}
    void Log(const std::string&) override {}
    void SubmitCommand(command::Command) override {}
    bool InWorld() const override { return false; }
    std::optional<std::string> ReadSetting(const std::string& mod, const std::string& key) const override
    {
        return mods::ReadModSettingFile(m_ini, mod, key);
    }

private:
    fs::path m_ini;
};

struct ModFiles {
    std::set<fs::path> documents;
    std::set<std::string> languages;
    std::set<std::string> layouts;
};

ModFiles CheckDocuments(const json_schema::Registry& schemas, Messages& errors)
{
    ModFiles files;
    for (const auto& mod : mod_pack::Active()) {
        std::set<fs::path> paths;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(mod.directory, ec), end; !ec && it != end; it.increment(ec))
            if (IsFile(it->path())) paths.insert(it->path());
        if (ec) errors.push_back(mod.directory.string() + ": " + ec.message());
        for (const auto& path : paths) {
            auto logical = path.lexically_relative(mod.directory);
            if (logical.extension() == ".png" && logical.parent_path() == mod_pack::kOverlayImages) {
                std::ifstream input(path, std::ios::binary);
                const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                png_read::Image image;
                std::string error;
                if (!png_read::Decode(bytes, image, error)) errors.push_back(path.string() + ": " + error);
            }
            // The converter's recipe loader validates these files and builds the outputs in memory.
            if (logical == "mod.json" || logical.extension() != ".json" || logical.parent_path() == "recipes") continue;
            std::ifstream input(path, std::ios::binary);
            const auto doc = json::parse(input, nullptr, false);
            const auto name = logical.filename().string();
            const bool patch = name.ends_with(".patch.json");
            const auto format = doc.is_object() ? doc.find("format") : doc.end();
            const auto kind = format != doc.end() && format->is_string() ? format->get<std::string>() : std::string();
            if (logical.parent_path() == "text") files.languages.insert(name.substr(0, name.size() - (patch ? 11 : 5)));
            if (logical.parent_path() == "gui" && patch && kind == gui_patch::kFormat)
                files.layouts.insert(name.substr(0, name.size() - 11));
            if (doc.is_discarded()) {
                errors.push_back(path.string() + ": invalid JSON");
                continue;
            }
            if (kind.starts_with("obl.") && !schemas.Find(kind) && !patch) errors.push_back(path.string() + ": unknown format " + kind);
            if (schemas.Find(kind)) {
                for (const auto& failure : schemas.Validate(kind, doc)) errors.push_back(path.string() + ": " + failure);
            }
            if (!patch)
                files.documents.insert(logical);
            else if (kind == "obl.json_patch")
                files.documents.insert(logical.parent_path() / (name.substr(0, name.size() - 11) + ".json"));
        }
    }
    return files;
}

void CheckGui(const fs::path& pack, const ModFiles& files, Messages& errors)
{
    std::string error;
    gui_layouts::Layouts layouts;
    if (!gui_layouts::LoadLayouts(pack, layouts, error)) {
        errors.push_back(error);
        return;
    }
    if (files.documents.contains("gui/layouts.json")) {
        for (const auto& [layout, choices] : layouts)
            for (const auto& [language, choice] : choices) {
                std::string bytes;
                if (!gui_layouts::ReadText(pack, choice, bytes, error)) errors.push_back(error);
            }
    }
    for (const auto& layout : files.layouts) {
        const auto found = layouts.find(layout);
        if (found == layouts.end()) {
            errors.push_back("gui/" + layout + ".patch.json: no layout named " + layout);
            continue;
        }
        std::set<std::string> seen;
        for (const auto& [language, choice] : found->second) {
            if (!seen.insert(choice.file).second) continue;
            std::string bytes;
            if (!gui_layouts::ReadText(pack, choice, bytes, error)) {
                errors.push_back(error);
                continue;
            }
            auto leading = own_layer::GuiPatches(layout);
            gui_patch::ApplyResult out;
            const auto& active = pack_path::ActiveMods();
            if (!leading.empty() &&
                    !gui_patch::ApplyPatches(bytes, leading, active, gui_patch::kReferenceWidth, gui_patch::kReferenceHeight, out, error))
                leading.clear();
            if (!gui_patch::ApplyPackPatches(
                        pack, layout, bytes, active, gui_patch::kReferenceWidth, gui_patch::kReferenceHeight, out, error, leading, &errors))
                errors.push_back(error);
        }
    }
    for (const auto& file : {"gui/credits.json", "gui/object_browser.json"}) {
        if (!files.documents.contains(file)) continue;
        gui_content::Content content;
        if (!gui_content::LoadPack(pack, content, error)) errors.push_back(error);
        break;
    }
}

std::set<std::string> CheckText(const fs::path& pack, ModFiles& files, Messages& errors)
{
    auto language = text_load::PackLanguage(pack);
    if (language.empty()) language = "en";
    files.languages.insert(language);
    std::set<std::string> keys;
    for (const auto& lang : files.languages) {
        std::vector<text::Entry> entries;
        std::string error;
        if (!text_load::LoadPack(pack, lang, entries, error, &errors)) errors.push_back(error);
        for (const auto& entry : entries) keys.insert(Lower(entry.key));
    }
    return keys;
}

std::set<std::string> CheckRecipes(const fs::path& pack, const json_schema::Registry& schemas, Messages& errors)
{
    std::set<std::string> outputs;
    const auto sources = convert::OpenDirectorySink(pack);
    for (const auto& mod : mod_pack::Active()) {
        std::error_code ec;
        if (!fs::is_directory(mod.directory / "recipes", ec)) continue;
        const auto input = convert::OpenDirectorySink(mod.directory);
        convert::MemorySink cache;
        convert::ModRecipesResult result;
        std::string error;
        const auto previous = errors.size();
        if (!convert::BuildModRecipes(*sources, *input, cache, schemas, result, error, &errors) && errors.size() == previous)
            errors.push_back(error);
        for (const auto& disabled : result.disabled) std::printf("mod \"%s\": %s\n", mod.manifest.id.c_str(), disabled.message.c_str());
        for (const auto& path : cache.Files())
            if (path.starts_with("sprites/") && path.ends_with(".json")) outputs.insert(fs::path(path).stem().string());
    }
    return outputs;
}

void CheckSprites(const fs::path& pack, const std::set<std::string>& sprites, const ModFiles& files,
        const std::set<std::string>& recipeSprites, Messages& errors)
{
    for (const auto& key : sprites) {
        sprite_pack::Sprite sprite;
        std::string error;
        // Decode assets shipped with the mod. References into the base pack only need its metadata.
        const bool changed = files.documents.contains(fs::path("sprites") / (key + ".json"));
        if (!changed && recipeSprites.contains(key)) continue;
        if (!sprite_pack::ReadSprite(pack / "sprites", key, false, sprite, error,
                    changed ? sprite_pack::ReadMode::Palettes : sprite_pack::ReadMode::Metadata)) {
            errors.push_back(error);
            continue;
        }
        if (!changed) continue;
        for (const auto& anim : sprite.anims) {
            std::vector<uint8_t> data;
            std::vector<uint32_t> offsets(anim.uniqueFrames);
            if (!sprite_pack::EncodeFrames(pack / "sprites", anim, 0, anim.uniqueFrames, 0, data, offsets, error)) errors.push_back(error);
        }
    }
}

void CheckScenario(const fs::path& pack, const fs::path& logical, const tables::WorldParams& params, bool catalogLoaded,
        const std::set<std::string>& text, std::set<std::string>& sprites, Messages& errors)
{
    scenario_load::Setup scenario;
    std::string error;
    if (!scenario_load::LoadPack(pack, logical.parent_path().filename().string(), scenario, error)) {
        errors.push_back(error);
        return;
    }
    const auto prefix = pack_path::Resolve(pack, logical).string() + ": ";
    for (const auto& building : scenario.buildings)
        if (catalogLoaded &&
                std::none_of(params.m_buildings.begin(), params.m_buildings.end(), [&](const auto& row) { return row.m_key == building; }))
            errors.push_back(prefix + "missing building reference " + building);
    const auto checkText = [&](const std::string& key) {
        if (!key.empty() && !text.contains(Lower(key))) errors.push_back(prefix + "missing text key " + key);
    };
    checkText(scenario.title);
    checkText(scenario.description);
    checkText(scenario.objectivesTitle);
    for (const auto& key : scenario.objectives) checkText(key);
    for (const auto& key : {scenario.islandTitle, scenario.islandArea, scenario.islandTemp, scenario.islandWeather, scenario.islandHazards})
        if (key) checkText(*key);
    if (scenario.pavement) sprites.insert(*scenario.pavement);
    if (scenario.beachEdge) sprites.insert(*scenario.beachEdge);
    std::string script;
    if (!scenario_load::ReadWorldScript(pack, scenario.script, script, error)) errors.push_back(prefix + error);
    if (!IsFile(pack_path::Resolve(pack, fs::path("guests") / (scenario.guestSet + ".json"))))
        errors.push_back(prefix + "missing guest set " + scenario.guestSet);
}

void CheckData(const fs::path& pack, const ModFiles& files, const std::set<std::string>& text, const std::set<std::string>& recipeSprites,
        Messages& errors)
{
    auto params = std::make_unique<tables::WorldParams>();
    std::string error;
    const bool catalogLoaded = catalog::LoadPack(pack, *params, error, &errors);
    if (!catalogLoaded) errors.push_back(error);
    std::set<std::string> sprites;
    for (const auto& logical : files.documents) {
        const auto file = pack_path::Resolve(pack, logical);
        data_patch::Input input(file);
        const auto value = json::parse(input, nullptr, false);
        const auto format = value.is_object() ? value.find("format") : value.end();
        if (format == value.end() || !format->is_string()) continue;
        const auto kind = format->get<std::string>();
        if (kind == "obl.scenario") CheckScenario(pack, logical, *params, catalogLoaded, text, sprites, errors);
        if (kind == "obl.campaign") {
            scenario_load::Campaign campaign;
            if (!scenario_load::LoadCampaign(pack, campaign, error))
                errors.push_back(error);
            else
                for (const auto& list : {campaign.campaign, campaign.sandboxes})
                    for (const auto& id : list)
                        if (!IsFile(pack_path::Resolve(pack, fs::path("scenarios") / id / "scenario.json")))
                            errors.push_back(file.string() + ": missing scenario " + id);
        }
        if (kind == "obl.guest_set" && guest_set::LoadPack(pack, logical.stem().string().c_str(), *params, error)) {
            for (const auto& guest : params->m_guests) {
                if (!guest.m_Sprite.empty()) sprites.insert(guest.m_Sprite);
                if (!guest.m_Script.empty() &&
                        !IsFile(pack_path::Resolve(pack, fs::path("scripts/object") / (Lower(guest.m_Script) + ".obs"))))
                    errors.push_back(file.string() + ": missing object script " + guest.m_Script);
            }
        } else if (kind == "obl.guest_set") {
            errors.push_back(error);
        }
        if (kind == "obl.sprite") sprites.insert(logical.stem().string());
        if (logical.begin()->string() == "catalog") {
            for (const auto* field : {"sprite", "icon_sprite", "icon_locked_sprite", "preview_sprite", "table_sprite", "chair_sprite"}) {
                const auto key = value.find(field);
                if (key != value.end() && key->is_string() && !key->get_ref<const std::string&>().empty())
                    sprites.insert(key->get<std::string>());
            }
            const auto script = value.find("object_script");
            if (script != value.end() && script->is_string() &&
                    !IsFile(pack_path::Resolve(pack, fs::path("scripts/object") / (Lower(script->get<std::string>()) + ".obs"))))
                errors.push_back(file.string() + ": missing object script " + script->get<std::string>());
        }
    }
    CheckSprites(pack, sprites, files, recipeSprites, errors);
    scenario_load::ScenarioRegistry registry;
    scenario_load::ScenarioRegistry::LoadFromPack(pack, registry, error, &errors);
}

void CheckPresentation(const fs::path& ini, Messages& errors)
{
    const auto& active = mod_pack::Active();
    const auto keys = key_actions::Resolve(mod_pack::ActionLayers(active), [&](std::string_view name) -> std::optional<std::string> {
        const auto key = ini::ReadString(ini, "Keys", name, "\x01", 256);
        return key == "\x01" ? std::nullopt : std::optional{key};
    });
    for (const auto& conflict : keys.conflicts) std::printf("mod conflict: %s\n", conflict.c_str());
    errors.insert(errors.end(), keys.problems.begin(), keys.problems.end());

    std::vector<mods::ScriptMod> scripts;
    for (const auto& mod : active) scripts.push_back({mod.manifest.id, mod.directory, {}, mod.manifest.settings, mod.manifest.takesOver});
    CheckHost host(ini);
    mods::ScriptRuntime runtime(host);
    runtime.Start(scripts, true);
    const auto& failures = runtime.Failures();
    errors.insert(errors.end(), failures.begin(), failures.end());
}

} // namespace

int CheckMods(const fs::path& gameDir, const fs::path& source)
{
    Messages errors;
    log::SetTap([](const char* line) {
        const std::string_view message(line);
        if (message.starts_with("mod patch: ") || message.starts_with("mod takes_over: ")) std::puts(line);
    });
    try {
        const auto directory = fs::absolute(gameDir).lexically_normal();
        const auto wide = directory.u16string();
        const Text16 gameDirectory(wide.begin(), wide.end());
        config::Load(gameDirectory, false);
        const auto pack = directory / pack_path::Selected();
        std::string error;
        if (!pack_path::CheckSelected(directory, error)) {
            errors.push_back("pack: " + error);
        } else if (!IsFile(pack / "pack.json")) {
            errors.push_back("pack: no pack.json in " + pack.string());
        } else {
            const auto modDirectory = source.empty() ? source : fs::absolute(source).lexically_normal();
            if (!mod_pack::Activate(directory, pack_path::SelectedId(), config::Get().mods, error, &errors, modDirectory) && !error.empty())
                errors.push_back(error);
            if (!mod_pack::Active().empty()) {
                json_schema::Registry schemas;
                if (!formats::LoadEmbeddedSchemas(schemas, error)) errors.push_back(error);
                auto files = CheckDocuments(schemas, errors);
                const auto text = CheckText(pack, files, errors);
                CheckGui(pack, files, errors);
                const auto recipeSprites = CheckRecipes(pack, schemas, errors);
                CheckData(pack, files, text, recipeSprites, errors);
                object::LoadScriptProgramVocabulary(gameDirectory, &errors);
                world::LoadWorldScriptVocabulary(gameDirectory);
                world::CheckModWorldScripts(&errors);
                CheckPresentation(directory / game::start_settings::kFileName, errors);
            }
        }
    } catch (const std::exception& failure) {
        errors.push_back(failure.what());
    }
    log::SetTap(nullptr);
    std::set<std::string> seen;
    for (const auto& error : errors)
        if (seen.insert(error).second) std::fprintf(stderr, "mod check: %s\n", error.c_str());
    std::printf("Mod check: %zu error(s)\n", seen.size());
    return seen.empty() ? 0 : 8;
}

} // namespace obl
