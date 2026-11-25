#include "game/mods/presentation_scripts.h"

#include "core/ascii_case.h"
#include "core/ini_file.h"
#include "core/log.h"
#include "core/os.h"
#include "formats/mod_pack.h"
#include "game/game/command_loop.h"
#include "game/game/game_accessors.h"
#include "game/game/game_context.h"
#include "game/game/game_host.h"
#include "game/game/start_settings.h"
#include "game/mods/game_snapshot.h"
#include "game/mods/input_events.h"
#include "game/mods/input_routing.h"
#include "game/mods/mod_settings_file.h"
#include "game/mods/overlay_draw.h"
#include "game/mods/script_runtime.h"
#include "game/objects/object_base.h"
#include "game/objects/object_registry_api.h"
#include "game/tables/world_params_leaves.h"
#include "game/tables/world_params_tables.h"
#include "game/ui/display_geometry.h"
#include "game/ui/gui_actions.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_modes_place.h"
#include "game/ui/gui_patch_runtime.h"
#include "game/ui/input_types.h"
#include "game/ui/map_view.h"
#include "game/ui/map_view_access.h"
#include "game/ui/renderer_types.h"
#include "game/world/world_params.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <utility>

namespace obl::mods {

namespace {

// GuiManager::m_mode of the place tools (gui_modes_place.cpp) and SetContext's slots for the picked building and scenery.
constexpr int kModePlaceScenery = 1, kModePlaceBuilding = 5, kModePlacePavement = 7, kModePlaceGrass = 8;
constexpr int kContextBuilding = 0, kContextScenery = 5;

// The settings the player's openbeach.ini holds for mods are `[Mod.<id>] <key>`. A scripted test run leaves the player's file alone,
// so what SetModSetting sets there stays in memory and is read first.
std::filesystem::path g_settingsFile;
std::map<std::string, std::string> g_testSettings; // "<mod>.<id>" -> value

std::unique_ptr<ScriptRuntime> g_runtime;
bool g_started = false;
InputEvents g_events;
std::vector<SyntheticInput> g_synthetic; // applied after the next input update, when the game reads them
std::vector<OverlayOp> g_overlay; // the drawing of the last frame
bool g_overlayShown = true;
std::optional<GameSnapshot> g_snapshot;
bool g_snapshotTaken = false;
std::string g_lastPointer;
int g_fingersDown = 0;
std::array<std::string, kAreaCount> g_areaOwners;

ScreenInfo CurrentScreen()
{
    ScreenInfo screen;
    const ui::Renderer* renderer = game::GetRenderer();
    if (!renderer) return screen;
    screen.width = renderer->m_width;
    screen.height = renderer->m_height;
    // The covered edges of the window, in the game's pixels: the image is letterboxed into the window, so a bar may already keep
    // it clear of a notch.
    int clientWidth = 0, clientHeight = 0, left = 0, top = 0, right = 0, bottom = 0;
    void* window = game::GetGameWindow();
    platform::GameWindowClientSize(window, clientWidth, clientHeight);
    platform::GameWindowSafeArea(window, left, top, right, bottom);
    const screen::Rect image = ui::ImageRect(screen.width, screen.height, clientWidth, clientHeight);
    const int imageWidth = image.right - image.left, imageHeight = image.bottom - image.top;
    if (imageWidth <= 0 || imageHeight <= 0) return screen;
    const auto scale = [](int covered, int total, int pixels) { return covered > 0 ? (covered * pixels + total - 1) / total : 0; };
    screen.safeLeft = scale(left - image.left, imageWidth, screen.width);
    screen.safeTop = scale(top - image.top, imageHeight, screen.height);
    screen.safeRight = scale(right - (clientWidth - image.right), imageWidth, screen.width);
    screen.safeBottom = scale(bottom - (clientHeight - image.bottom), imageHeight, screen.height);
    return screen;
}

bool PlaceTool(const GuiRequest& request)
{
    ui::GuiManager* gui = game::GetGuiManager();
    tables::WorldParams* params = game::GetWorldParams();
    if (!gui || !game::GetWorld() || !params) return false;
    if (request.kind == "building") {
        for (int i = 0; i < params->m_numBuildings; ++i)
            // Catalog keys are compared as the game's own lookups compare them, ignoring case.
            if (ascii::EqualsNoCase(world::world_params::GetBuildingDef(params, i)->m_key, request.name)) {
                gui->SetContext(kContextBuilding, i);
                gui->SetMode(kModePlaceBuilding);
                return true;
            }
        return false;
    }
    for (int i = 0; i < params->m_numScenery; ++i) {
        const tables::SceneryDef* def = world::world_params::GetSceneryDef(params, i);
        if (!ascii::EqualsNoCase(def->m_key, request.name)) continue;
        gui->SetContext(kContextScenery, i);
        // Paths and grass (category 1) are painted, other scenery is placed, as the scenery panel picks the tool.
        gui->SetMode(def->m_Category != 1 ? kModePlaceScenery : gui::IsGrass(*def) ? kModePlaceGrass : kModePlacePavement);
        return true;
    }
    return false;
}

bool SelectObject(uint32_t id)
{
    object::Manager* manager = game::GetObjectManager();
    if (!manager || !game::GetWorld()) return false;
    object::Base* object = object::object_manager::ObjectById(manager, id);
    if (!object || !object->IsSelectable()) return false;
    // What the player's click does: the object's info dialog opens, with the object as its provider.
    object->OnSelect(1, 0);
    return true;
}

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

    void SynthesizeInput(const SyntheticInput& input) override { g_synthetic.push_back(input); }

    std::optional<CameraView> Camera() const override
    {
        const ui::MapView* view = game::GetWorld() ? ui::GetMapView() : nullptr;
        const ui::Renderer* renderer = game::GetRenderer();
        if (!view || !renderer) return std::nullopt;
        // The position a move sets at once (clamped); m_cameraX/Y, what drawing uses, follow it in the world's next update.
        return CameraView{static_cast<int>(view->m_cameraPositionX), static_cast<int>(view->m_cameraPositionY), renderer->m_zoomLevel};
    }

    bool SetCameraPosition(int x, int y) override
    {
        ui::MapView* view = game::GetWorld() ? ui::GetMapView() : nullptr;
        if (!view) return false;
        view->SetCameraPos(x, y); // clamped to the map like every camera move
        return true;
    }

    // The request the zoom buttons post; the world's next update applies it and keeps the view's centre.
    bool SetZoomLevel(int level) override
    {
        if (!game::GetWorld()) return false;
        game::GetContext().view.m_zoomRequest = level;
        return true;
    }

    bool Gui(const GuiRequest& request) override
    {
        namespace gui = gui_patch_runtime;
        ui::GuiManager* manager = game::GetGuiManager();
        if (!manager) return false;
        using Op = GuiRequest::Op;
        switch (request.op) {
        case Op::ShowControl: return gui::SetControlVisible(manager, request.name, true);
        case Op::HideControl: return gui::SetControlVisible(manager, request.name, false);
        case Op::MoveControl: return gui::MoveControl(manager, request.name, request.x, request.y);
        case Op::OpenDialog: return gui::ShowDialog(manager, request.name);
        case Op::CloseDialog: return gui::HideDialog(manager, request.name);
        case Op::MoveDialog: return gui::MoveDialog(manager, request.name, request.x, request.y);
        case Op::Select: return SelectObject(request.id);
        case Op::Place: return PlaceTool(request);
        case Op::CancelTool:
            if (!game::GetWorld()) return false;
            manager->SetMode(gui_actions::kModeDefault);
            return true;
        }
        return false;
    }

    void Draw(const OverlayOp& op) override { g_overlay.push_back(op); }

    std::optional<std::pair<int, int>> MeasureText(const std::string& text, const std::string& font) override
    {
        return MeasureOverlayText(text, font);
    }

    ScreenInfo Screen() const override { return CurrentScreen(); }
    std::string LastPointer() const override { return g_lastPointer; }

    const GameSnapshot* Snapshot() override
    {
        if (!g_snapshotTaken) {
            g_snapshot = TakeGameSnapshot();
            g_snapshotTaken = true;
        }
        return g_snapshot ? &*g_snapshot : nullptr;
    }
};

GameScriptHost g_host;

// The taken-over defaults follow the running mods: a mod that is switched off gives its areas back at once.
void SyncAreas()
{
    for (int i = 0; i < kAreaCount; ++i) {
        const std::string owner = g_runtime ? g_runtime->AreaOwner(mod_pack::kTakeOverAreas[i]) : std::string();
        if (owner == g_areaOwners[i]) continue;
        if (!g_areaOwners[i].empty() && owner.empty())
            OBL_LOG("presentation scripts: \"%s\" is the engine's again (mod \"%s\" stopped)",
                    std::string(mod_pack::kTakeOverAreas[i]).c_str(), g_areaOwners[i].c_str());
        else if (!owner.empty())
            OBL_LOG("presentation scripts: mod \"%s\" takes over \"%s\"", owner.c_str(), std::string(mod_pack::kTakeOverAreas[i]).c_str());
        g_areaOwners[i] = owner;
        SetTakenOver(static_cast<Area>(i), !owner.empty());
    }
}

// The platform's finger events, before its own touch recognizer (input_routing.h).
bool RouteTouchToScripts(const TouchEvent& touch)
{
    if (!g_runtime) return false;
    if (touch.phase == TouchPhase::Down) ++g_fingersDown;
    if (touch.phase == TouchPhase::Up && g_fingersDown > 0) --g_fingersDown;
    if (touch.phase == TouchPhase::Cancel) g_fingersDown = 0;
    g_lastPointer = "touch";
    const bool consumed = !g_runtime->Input(ToInputEvent(touch)).empty();
    SyncAreas();
    return consumed;
}

void Start()
{
    g_started = true;
    std::vector<ScriptMod> mods;
    for (const mod_pack::ActiveMod& mod : mod_pack::Active()) {
        // The store sits beside mods/, outside saves and outside the mod's directory, so updating the mod keeps it.
        const std::filesystem::path gameDirectory = mod.directory.parent_path().parent_path();
        g_settingsFile = gameDirectory / game::start_settings::kFileName;
        mods.push_back({mod.manifest.id, mod.directory, gameDirectory / "mod-data" / (mod.manifest.id + ".json"), mod.manifest.settings,
                mod.manifest.takesOver});
    }
    if (mods.empty()) return;
    g_runtime = std::make_unique<ScriptRuntime>(g_host);
    g_runtime->Start(mods);
    SetTouchHandler(&RouteTouchToScripts);
    gui_patch_runtime::SetModActionHandler([](std::string_view modId, std::string_view event) {
        if (!g_runtime) return;
        g_runtime->Action(modId, event);
        SyncAreas();
    });
    SyncAreas();
}

} // namespace

void PresentationInput(ui::Input* input)
{
    if (!g_started) Start();
    if (!g_runtime) return;
    g_snapshotTaken = false;
    if (input) {
        for (const InputEvent& event : g_events.Take(*input)) {
            if ((event.kind == "mouse" || event.kind == "pointer") && g_fingersDown == 0) g_lastPointer = "mouse";
            if (!g_runtime->Input(event).empty()) g_events.Consume(*input, event);
        }
        for (const SyntheticInput& synthetic : std::exchange(g_synthetic, {})) g_events.Apply(*input, synthetic);
        g_events.HoldPointer(*input);
    }
    SyncAreas();
}

void PresentationFrame(float dt)
{
    if (!g_started) Start();
    if (!g_runtime) return;
    g_snapshotTaken = false; // the tick ran since the input
    g_overlay.clear();
    g_runtime->Frame(dt);
    SyncAreas();
}

void DrawPresentationOverlay()
{
    ui::Renderer* renderer = game::GetRenderer();
    if (!g_overlayShown || g_overlay.empty() || !renderer) return;
    DrawOverlayOps(*renderer, g_overlay);
}

void SetOverlayShown(bool shown)
{
    g_overlayShown = shown;
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
    SetTouchHandler(nullptr);
    gui_patch_runtime::SetModActionHandler(nullptr);
    SyncAreas();
    g_events.Reset();
    g_synthetic.clear();
    g_overlay.clear();
    ReleaseOverlayResources();
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
