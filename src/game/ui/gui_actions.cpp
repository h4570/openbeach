#include "game/ui/gui_manager_buttons.h"
#include "game/ui/gui_widget_virtual.h"
#include "game/ui/gui_manager_frame.h"
#include "game/ui/gui_manager_dialogs.h"
#include "game/ui/sprite_bank.h"
#include "game/ui/sound_api.h"
#include "game/ui/renderer_api.h"
#include "game/ui/gui_action_entries.h"
#include "game/ui/save_slots.h"
#include "game/game/game_api.h"
#include "game/game/settings_store.h"
#include "game/ui/gui_controls.h"
#include "game/world/world_queries.h"
#include "game/objects/building_api.h"
#include "game/world/world_queries_api.h"
#include "game/map/grid_state.h"
#include "game/objects/object_registry_api.h"
#include "game/ui/music_player.h"
#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/game/host_text.h"
#include "game/game/command_loop.h"
#include "game/ui/gui_actions.h"
#include "game/ui/sound_types.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_containers_types.h"
#include "game/ui/font.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "core/log.h"
#include "game/objects/staff_wages.h"
#include "core/ascii_case.h"
#include "core/call_counter.h"
#include "game/messages/message_handler.h"
#include "game/world/world_params.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace obl::gui {
namespace {
using namespace gui_actions;

test::CallCounter g_counter("ui.gui-actions"); // call counter
const world_data::World* g_refreshWorld = nullptr;
uint32_t g_refreshTick = 0;

ui::GuiManager* Gui()
{
    return game::GetGuiManager();
}

struct PanelFields {
    uint8_t* enabled;
    uint8_t* flag;
    uint8_t* dirty;
    ui::GuiWidget** widget;
    ui::GuiWidget** button;
    ui::SpriteInstance** sprite;
};
PanelFields Fields(Panel panel)
{
    auto* gui = Gui();
    if (panel == Panel::Radar)
        return {&gui->m_radarEnabled, &gui->m_radarBig, &gui->m_radarDirty, &gui->m_radarWidget, &gui->m_radarButton, &gui->m_radarSprite};
    return {&gui->m_progressVisible, &gui->m_progressBig, &gui->m_progressDirty, &gui->m_progressDialog, &gui->m_progressButton,
            &gui->m_progressSprite};
}

// The widget's virtual Invalidate: sets the redraw flag, containers recurse.
void Invalidate(void* widget)
{
    (static_cast<ui::GuiWidget*>(widget))->Invalidate();
}

struct LiveEnv {
    static void* Bank() { return ui::GetSpriteBank(); }
    static audio::Sound* Sound() { return audio::CurrentSound(); }

    float MaxDistance() { return Sound()->maxDistance; }
    void WriteMaxDistance(float value) { Sound()->maxDistance = value; }
    int ZoomLevel() { return obl::ui::renderer::GetZoomLevel(); }
    void WriteZoomRequest(int request) { game::GetContext().view.m_zoomRequest = request; }
    void WriteCameraGrab(bool grab) { Gui()->m_cameraGrabRequested = grab; }
    bool HasProvider() { return Gui()->m_variableProvider != nullptr; }
    int ProviderId() { return static_cast<int>(static_cast<object::Base*>(Gui()->m_variableProvider)->m_id); }
    int Selection() { return Gui()->m_selection; }
    void WriteSelection(int selection) { Gui()->m_selection = selection; }

    bool IsDialogOpen(const char* name) { return static_cast<ui::GuiManager*>(Gui())->IsDialogOpen(name); }
    void CloseDialog(const char* name) { static_cast<ui::GuiManager*>(Gui())->CloseDialog(name); }
    void ShowDialog(const char* name, bool modal) { Gui()->ShowDialog(name, nullptr, modal); }
    void CloseAllDialogs() { Gui()->CloseAllDialogs(); }
    bool ToggleDialog(const char* name) { return Gui()->ToggleDialog(name, nullptr, false); }
    void SetMode(int mode) { Gui()->SetMode(mode); }
    bool IsSandbox() { return game::GetGame()->m_sandbox != 0; }
    void SetButtonGraphic(const char* button, const char* graphic) { Gui()->SetActionButtonGraphic(button, graphic); }
    void SetTickerVisible(bool visible) { static_cast<ui::TickerList*>(Gui()->m_tickerList)->SetVisible(visible); }
    void ToggleMusicPanel() { Gui()->ToggleMusicPanel(); }
    void MusicCommand(int command) { static_cast<audio::MusicPlayer*>(Gui()->m_musicPlayer)->Command(command); }
    float MusicVolume() { return Sound()->musicVolume; }
    void SetMusicVolume(float volume)
    {
        Sound()->SetMusicVolume(volume);
        game::settings_store::Current().m_musicVolume = volume;
    }

    void CloseDialogById(int id) { Gui()->CloseDialogById(id); }
    void PostDialogClosed()
    {
        messages::Message message{};
        message.sender = message.receiver = -2;
        message.type = messages::MT_DIALOG_CLOSED;
        messages::message_facade::Post(messages::message_facade::Current(), &message);
    }
    void ClosePauseDialog() { Gui()->ClosePauseDialog(); }
    void WritePaused(bool paused) { game::SubmitCommand(command::SetPaused{paused}); }
    void ResumeGame() { game::SubmitCommand(command::ResumeGame{}); }
    void ToggleInGameOptions() { Gui()->ToggleInGameOptions(); }
    void SaveSettings() { game::settings_store::Save(game::settings_store::Current()); }
    void WriteVideoDefaults()
    {
        auto& settings = game::settings_store::Current();
        settings.m_nudgeScroll = settings.m_seaEffects = settings.m_lightEffects = 1;
        settings.m_keyline = 0;
    }
    void ApplySeaEffects(bool enabled) { obl::ui::renderer::SetSeaEffects(game::GetRenderer(), enabled); }
    void WriteSettingsMusicVolume(float volume) { game::settings_store::Current().m_musicVolume = volume; }
    void WriteSettingsEffectsVolume(float volume) { game::settings_store::Current().m_effectsVolume = volume; }
    void ApplyMusicVolume(float volume) { Sound()->SetMusicVolume(volume); }
    void ApplyEffectsVolume(float volume) { Sound()->SetEffectsVolume(volume); }

    bool HasWorld() { return game::GetWorld() != nullptr; }
    void InvalidateDialog(const char* name) { static_cast<ui::GuiManager*>(Gui())->InvalidateDialog(name, 0); }
    void ScanSaveSlots() { ui::save_panel::ScanSaveSlots(); }
    void ClearSaveSlots() { ui::save_panel::ClearSaveSlots(); }
    void FillLoadList() { ui::save_panel::FillLoadList(); }
    void FillSaveList() { ui::save_panel::FillSaveList(); }
    bool SaveSlotExists() { return ui::save_panel::IsExistingSlotSelected(); }
    void WriteSave() { ui::save_panel::WriteSave(); }
    void RequestLoadSelectedSave() { ui::save_panel::RequestLoadSelected(); }
    void DeleteSelectedSave() { ui::save_panel::DeleteSelectedSave(); }
    void DeleteSelectedLoad() { ui::save_panel::DeleteSelectedLoad(); }
    void ToggleLoadMenu() { Gui()->ToggleLoadGameMenu(); }
    void ToggleSaveMenu() { Gui()->ToggleSaveGameMenu(); }
    void ToggleBuildPanel() { Gui()->ToggleBuildPanel(); }
    void ToggleSceneryPanel() { Gui()->ToggleSceneryPanel(); }
    void AnimateDialog(const char* name, int type, int delay) { Gui()->AnimateDialog(name, type, kPanelAnimationSpeed, delay); }
    bool DialogMinimized(const char* name) { return static_cast<ui::GuiManager*>(Gui())->GetDialogMinimized(name) != 0; }
    void SetDialogMinimized(const char* name, bool minimized) { Gui()->SetDialogMinimized(name, minimized); }
    void FlashButtons(const char* action, bool flash) { Gui()->FlashActionButtons(action, flash); }
    void SaveGridMap() { game::GetGrid()->SaveTileClassMap(); }
    int BuildingId() { return Gui()->m_context[0]; }
    static const tables::BuildingDef* Definition(int id) { return world::world_params::GetBuildingDef(game::GetWorldParams(), id); }
    bool BuildingLockedByTree(int id) { return game::GetWorld()->BuildingLockedByTree(id); }
    bool BuildingAllowed(int id) { return game::GetWorld()->BuildingAllowed(id); }
    double Cash() { return game::GetWorld()->m_player->m_cash; }
    int BuildingCost(int id) { return Definition(id)->m_Cost2Build; }
    bool BuildingHasFootprint(int id) { return Definition(id)->m_footprint != nullptr; }
    void WriteRequestedMode(int mode) { Gui()->m_requestedMode = mode; }

    using ObjectList = object::Base**;
    static object::Base* Obj(void* object) { return static_cast<object::Base*>(object); }
    static object::Manager* ObjectManager() { return game::GetObjectManager(); }
    void* Provider() { return Gui()->m_variableProvider; }
    uint32_t ObjectId(void* object) { return object ? Obj(object)->m_id : 0; }
    void* ObjectById(uint32_t id) { return object::object_manager::ObjectById(ObjectManager(), id); }
    void ClearSelectionByClass(int objectClass) { object::object_manager::ClearSelectionByClass(ObjectManager(), objectClass); }
    void SelectDialogTab(const char* name, int tab) { Gui()->SelectDialogTab(name, tab); }
    void ShowDialogFor(const char* name, void* object, bool modal) { Gui()->ShowDialog(name, object, modal); }
    void CloseDialogByProvider(void* object) { Gui()->CloseDialogByProvider(object); }
    // ConfirmSell: the `demolish` command, applied at the start of the next tick (docs/architecture/commands.md).
    void BeginDemolish(void* object) { game::SubmitCommand(command::Demolish{ObjectId(object)}); }
    void OpenBuilding(void* object) { game::SubmitCommand(command::SetBuildingOpen{ObjectId(object), true}); }
    void CloseBuilding(void* object) { game::SubmitCommand(command::SetBuildingOpen{ObjectId(object), false}); }
    void BreakObject(void* object) { game::SubmitCommand(command::BreakObject{ObjectId(object)}); }
    void AddUrgentRepair(uint32_t id) { game::SubmitCommand(command::CallRepair{id}); }
    void AddUrgentCleaning(uint32_t id) { game::SubmitCommand(command::CallCleaner{id}); }
    // The paired flags belong to the repair/clean command, along with the urgent job.
    void SetCalledRepairing(void*, bool) {}
    void SetCalledCleaning(void*, bool) {}
    uint32_t AdjacentBuilding(uint32_t id, bool forward) { return game::GetWorld()->AdjacentBuilding(id, forward); }
    bool BuildingMenuIsNone(void* object)
    {
        return ascii::EqualsNoCase(static_cast<object::Building*>(object)->m_definition->m_Menu, kNoneMenu);
    }
    // the object's virtual OnSelect(clicked, centerCamera), called with (1, 1) as the buttons do
    void SelectObject(void* object) { provider_virtual::Select(object, 1, 1); }
    int StaffBrowserType() { return Gui()->m_context[10]; }
    int HireType() { return Gui()->m_context[11]; }
    int BrowsedStaffId() { return Gui()->m_context[12]; }
    void SetContext(int index, int value) { Gui()->SetContext(index, value); }
    int StaffClass(void* object) { return static_cast<int>(Obj(object)->m_type); }
    uint32_t AdjacentStaff(uint32_t id, int objectClass, bool forward) { return game::GetWorld()->AdjacentStaff(id, objectClass, forward); }
    uint32_t HomeId(void* object) { return static_cast<uint32_t>(static_cast<object::Staff*>(object)->m_HomeID); }
    void IncShift(void* object)
    {
        const auto& member = *static_cast<object::Staff*>(object);
        int32_t start = member.m_StartTime, end = member.m_EndTime;
        staff::IncrementShiftHours(start, end);
        SetShift(object, start, end);
    }
    void DecShift(void* object)
    {
        const auto& member = *static_cast<object::Staff*>(object);
        int32_t start = member.m_StartTime, end = member.m_EndTime;
        staff::DecrementShiftHours(start, end);
        SetShift(object, start, end);
    }
    std::vector<void*> ObjectsOfClass(int objectClass)
    {
        const auto list = object::object_manager::GetListByClass(ObjectManager(), objectClass);
        return std::vector<void*>(list.begin(), list.end());
    }
    bool HasStaffOfClass(int objectClass) { return !ObjectsOfClass(objectClass).empty(); }
    // gui_manager_buttons::SelectStaffByClass keeps one rotating index per button in the game context
    uint32_t SelectStaffByClass(int objectClass, int slot)
    {
        return ui::GuiManager::SelectStaff(objectClass, game::GetContext().staffSelectIndex.data() + slot);
    }
    void ToggleSelection(int id) { Gui()->m_selection = ToggledSelection(Gui()->m_selection, id); }
    std::vector<void*> HomeBuildings(const char* name)
    {
        const auto list = object::object_manager::FindObjectsByName(ObjectManager(), name);
        return std::vector<void*>(list.begin(), list.end());
    }
    int Wage(void* object) { return static_cast<object::Staff*>(object)->m_Wage; }
    void WriteWage(void* object, int wage) { game::SubmitCommand(command::CopyStaffWage{ObjectId(object), wage}); }
    // Happiness is recalculated by copy-staff-wage at application time.
    void RecalculateHappiness(void*) {}
    int ShiftStart(void* object) { return static_cast<object::Staff*>(object)->m_StartTime; }
    int ShiftEnd(void* object) { return static_cast<object::Staff*>(object)->m_EndTime; }
    void SetShift(void* object, int start, int end) { game::SubmitCommand(command::SetStaffShift{ObjectId(object), start, end}); }
    uint32_t HomeCapacity(const char* name)
    {
        const int id = world::world_params::GetBuildingId(game::GetWorldParams(), name);
        return static_cast<uint32_t>(Definition(id)->m_MaxStaff);
    }
    uint32_t StaffCount(void* object)
    {
        const auto* building = static_cast<object::Building*>(object);
        return static_cast<uint32_t>(building->m_numFemalestaff + building->m_numMalestaff);
    }
    bool HasBuildJob(void* object) { return object::building_api::HasBuildJob(static_cast<const object::Building*>(object)); }
    bool HasDemolishJob(void* object) { return object::building_api::HasDemolishJob(static_cast<const object::Building*>(object)); }
    // Dispatch submits the hire even when the GUI prediction finds no room; Run keeps the feedback only.
    void HireStaff(void*) {}
    void DismissStaff(void*, uint32_t staffId) { game::SubmitCommand(command::DismissStaff{staffId}); }
    void RefreshAllDialogs()
    {
        g_refreshWorld = game::GetWorld();
        g_refreshTick = game::GetCommandLoopStats().tick;
    }
    void ShowMessageBox(const char* key) { Gui()->ShowMessageBox("MessageOKBox", key, nullptr, true); }

    void SetNextState(int state) { game::game_api::SetNextState(state); }
    void SetSandboxMode(bool sandbox) { game::game_api::SetSandboxMode(sandbox); }
    void ResetScenarioProgress() { game::game_api::ResetScenarioProgress(); }
    void SaveScenarioProgress() { game::game_api::SaveScenarioProgress(); }
    void PostQuit() { ::obl::ui::host::RequestQuit(0); }
    int Rand() { return obl::game::Rand(); }
    void CreateCredits() { (static_cast<ui::GuiManager*>(Gui()))->CreateCredits(); }
    void DestroyCredits() { (static_cast<ui::GuiManager*>(Gui()))->DestroyCredits(); }

    bool PanelEnabled(Panel panel) { return *Fields(panel).enabled != 0; }
    bool PanelFlag(Panel panel) { return *Fields(panel).flag != 0; }
    void PanelWriteFlag(Panel panel, bool flag) { *Fields(panel).flag = flag; }
    void PanelWriteDirty(Panel panel) { *Fields(panel).dirty = 1; }
    void PanelReleaseSprite(Panel panel) { (static_cast<ui::SpriteBank*>(Bank()))->ReleaseInstance(*Fields(panel).sprite); }
    void PanelCreateSprite(Panel panel, const char* name)
    {
        *Fields(panel).sprite = (static_cast<ui::SpriteBank*>(Bank()))->CreateInstance(name);
    }
    void PanelSetGraphic(Panel panel, const char* action, const char* graphic)
    {
        button_action_facade::SetGraphicByAction(static_cast<ui::GuiButton*>(*Fields(panel).button), action, graphic);
    }

    void PanelInvalidateWidget(Panel panel)
    {
        if (void* widget = *Fields(panel).widget) Invalidate(widget);
    }
    void PanelInvalidateButton(Panel panel) { Invalidate(*Fields(panel).button); }
    void ClearRadarSurface() { obl::ui::renderer::ClearSurface(Gui()->m_renderer, Gui()->m_radarSurface); }
    void WriteRadarEnabled(bool enabled) { Gui()->m_radarEnabled = enabled; }
    void WriteProgressEnabled(bool enabled) { Gui()->m_progressVisible = enabled; }
};

bool Runnable(Action action)
{
    LiveEnv env;
    if (NeedsProvider(action)) {
        void* provider = Gui()->m_variableProvider;
        if (!provider) return false;
        if (NeedsHome(action)) {
            const uint32_t home = env.HomeId(provider);
            if (home == 0xffffffffu || !env.ObjectById(home)) return false;
        }
    }
    if (NeedsBrowsedStaff(action) && !env.ObjectById(static_cast<uint32_t>(env.BrowsedStaffId()))) return false;
    if (const StaffIcon icon = IconOf(action); icon.objectClass >= 0 && !env.HasStaffOfClass(icon.objectClass)) return false;
    return true;
}

void Dispatch(Action action, int arg)
{
    if (!Runnable(action)) return;
    LiveEnv env;
    if (action == Action::HireNewStaff) {
        constexpr const char* kProfessions[] = {"Cleaner", "Builder", "Mechanic", "Rep", "Lifeguard", "SecurityGuard"};
        const int type = env.HireType();
        if (type >= 0 && type < 6) game::SubmitCommand(command::HireStaff{kProfessions[type]});
    }
    Run(env, action, arg);
    g_counter.Count();
}

// `call-drive GuiActionConfirmSell <id>`: the confirm button of the ConfirmDemolish dialog with object <id> as its provider (the provider is
// put back after), for the test of the `demolish` command the button submits.
test::CallCounter g_confirmSell("GuiActionConfirmSell");
void DriveConfirmSell(int id)
{
    LiveEnv env;
    void* object = game::GetWorld() ? env.ObjectById(static_cast<uint32_t>(id)) : nullptr;
    if (!object) {
        OBL_LOG("autotest: FAILED GuiActionConfirmSell needs gameplay and an object id");
        return;
    }
    void* saved = Gui()->m_variableProvider;
    Gui()->m_variableProvider = object;
    Dispatch(Action::ConfirmSell, 0);
    Gui()->m_variableProvider = saved;
    g_confirmSell.Count();
}

template<Action action> void DriveCommandAction(int id)
{
    LiveEnv env;
    void* object = game::GetWorld() ? env.ObjectById(static_cast<uint32_t>(id)) : nullptr;
    if (!game::GetWorld() || (action != Action::HireNewStaff && !object)) {
        OBL_LOG("autotest: FAILED GuiAction command driver needs gameplay and an object id");
        return;
    }
    auto* gui = Gui();
    void* savedProvider = gui->m_variableProvider;
    const int savedBrowsed = gui->m_context[12], savedHire = gui->m_context[11];
    gui->m_variableProvider = object;
    gui->m_context[12] = id;
    if constexpr (action == Action::HireNewStaff) gui->m_context[11] = id;
    Dispatch(action, -1);
    gui->m_variableProvider = savedProvider;
    gui->m_context[12] = savedBrowsed;
    gui->m_context[11] = savedHire;
}

// These drivers run the real action with a temporary dialog provider (or hire type).
// clang-format off
#define OBL_COMMAND_ACTION_DRIVERS(X) \
    X(HireNewStaff) X(DismissConfirm) X(OpenBuilding) X(CloseBuilding) X(CallRepair) X(CallCleaner) \
    X(IncStaffHours) X(DecStaffHours) X(SetStaffWages) X(SetStaffShift) X(StaffSetAllWages) X(StaffSetAllShift) X(ClosePause) \
    X(ExitDialogResume) X(BreakObject)
#define OBL_COMMAND_COUNTER(Id) test::CallCounter g_command##Id("GuiAction" #Id);
OBL_COMMAND_ACTION_DRIVERS(OBL_COMMAND_COUNTER)
#undef OBL_COMMAND_COUNTER
// clang-format on
} // namespace

void RefreshCommandDialogs()
{
    if (!g_refreshWorld) return;
    const auto stats = game::GetCommandLoopStats();
    if (g_refreshWorld != game::GetWorld() || !stats.inWorld) {
        g_refreshWorld = nullptr;
        return;
    }
    if (stats.tick == g_refreshTick) return;
    // The roster is refreshed only after the hire/dismiss tick, so newly created members are in the list.
    g_refreshWorld = nullptr;
    static_cast<ui::GuiManager*>(Gui())->RefreshAllDialogs();
}

bool RegisterGuiActionDrivers()
{
    g_confirmSell.SetDriver(DriveConfirmSell);
#define OBL_COMMAND_DRIVER(Id) g_command##Id.SetDriver(DriveCommandAction<Action::Id>);
    OBL_COMMAND_ACTION_DRIVERS(OBL_COMMAND_DRIVER)
#undef OBL_COMMAND_DRIVER
#undef OBL_COMMAND_ACTION_DRIVERS
    return true;
}

namespace gui_action_entries {
#define OBL_DEFINE_ACTION(Id, Symbol)                                                                                                      \
    void __cdecl Symbol(int arg)                                                                                                           \
    {                                                                                                                                      \
        Dispatch(Action::Id, arg);                                                                                                         \
    }
OBL_GUI_ACTIONS(OBL_DEFINE_ACTION)
#undef OBL_DEFINE_ACTION
} // namespace gui_action_entries
} // namespace obl::gui
