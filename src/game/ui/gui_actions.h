#pragma once

#include <cstdint>

namespace obl::gui_actions {

enum class Action : uint8_t {
    IncDist,
    DecDist,
    CameraMode,
    DoCameraGrab,
    LockCamera,
    ZoomIn,
    ZoomOut,
    ToggleRadarSize,
    ToggleProgressSize,
    ShowTickerList800,
    ShowTickerList640,
    MusicList,
    MusicRefresh,
    MusicRefreshConfirm,
    MusicVolUp,
    MusicVolDown,
    MusicSun,
    MusicMoon,
    MusicRepeat,
    MusicShuffle,
    MusicDelete,
    MusicMoveUp,
    MusicMoveDown,
    MusicPlay,
    MusicNext,
    MusicPrevious,
    MusicStop,
    MusicPause,
    MusicDefault,
    ExitDialog,
    ExitDialogResume,
    ExitDialogMessage,
    ClosePause,
    ShowGoal,
    InGameOptions,
    ExitOptions,
    ResetVideoOptions,
    ResetAudioOptions,
    LoadGameMenu,
    LoadGamePanel,
    SaveGamePanel,
    ExitLoadGame,
    LoadGame,
    SaveGame,
    SaveGameConfirm,
    DeleteSaveGame,
    DeleteLoadGame,
    DeleteSaveGameConfirm,
    DeleteLoadGameConfirm,
    ExitSaveOK,
    BuildList,
    PlaceBuilding,
    SaveGridMap,
    PaintMap,
    ExitBuildMode,
    SceneryPanel,
    ExitSceneryPanel,
    MinSceneryPanel,
    SceneryDozer,
    ObjectBrowser,
    GuestBrowser,
    SellButton,
    ConfirmSell,
    BreakObject,
    CallRepair,
    CallCleaner,
    OpenBuilding,
    CloseBuilding,
    NextBuilding,
    PrevBuilding,
    PowerPanel,
    MoneyPanel,
    IslandStatus,
    FerryPanel,
    CleanerBrowser,
    BuilderBrowser,
    MechanicBrowser,
    RepBrowser,
    LifeguardBrowser,
    SecurityBrowser,
    HireNewStaff,
    DismissStaff,
    DismissConfirm,
    StaffFire,
    NextStaff,
    PrevStaff,
    StaffSend,
    StaffHome,
    IncStaffHours,
    DecStaffHours,
    SetStaffWages,
    SetStaffShift,
    StaffSetAllWages,
    StaffSetAllShift,
    CleanerIcon,
    BuilderIcon,
    MechanicIcon,
    RepIcon,
    LifeguardIcon,
    SecurityIcon,
    None,
    StartNewGame,
    ExitWindows,
    ExitWindowsConfirm,
    ShowMainMenu,
    ExtrasMenu,
    ShowScenSel,
    PlaySandBox,
    ResetGame,
    ResetGameConfirm,
    ExitGame,
    ExitGameConfirm,
    WonGame,
    CreditsMenu,
    ResetProgress,
    ResetProgressConfirm,
    Count
};

enum class Panel : uint8_t { Radar, Progress };

// Commands of MusicPlayer::Command (it accepts 16 values; 8 and 9 switch a setting on and off and have no GUI action, so they have
// no name here).
enum MusicCommand : int {
    MusicSun = 0,
    MusicMoon = 1,
    MusicRepeat = 2,
    MusicShuffle = 3,
    MusicDelete = 4,
    MusicMoveUp = 5,
    MusicMoveDown = 6,
    MusicPlay = 7,
    MusicNext = 10,
    MusicPrevious = 11,
    MusicStop = 12,
    MusicPause = 13,
    MusicRefresh = 14,
    MusicDefault = 15,
};

inline constexpr float kDistanceStep = 5.0f;
inline constexpr float kVolumeStep = 0.1f;
inline constexpr int kModeCameraControls = 2; // GuiManager::SetMode argument: the photo camera
inline constexpr int kModeDefault = 4;
inline constexpr int kModePaint = 0;
inline constexpr int kModeSceneryDozer = 10;
inline constexpr int kModePlaceBuilding = 5; // what ShowCancelBuildMode requests for a building that has a footprint
// Staff type of the staff browser / HireNewStaff (GuiManager context 10 and 11), also the order of the hire table.
enum StaffType : int { StaffCleaner = 0, StaffBuilder = 1, StaffMechanic = 2, StaffRep = 3, StaffLifeguard = 4, StaffSecurity = 5 };
inline constexpr int kModeStaffSend = 9; // GuiManager::SetMode argument: pick where to send the staff member (context 9 holds its id)
inline constexpr int kContextSendStaff = 9, kContextStaffBrowserType = 10, kContextHireType = 11;
inline constexpr int kObjectClassGuest = 3; // ObjectManager class of the guests (2 = buildings)
inline constexpr const char* kStaffBrowserDialog = "StaffBrowser";
inline constexpr const char* kConfirmDismissDialog = "ConfirmDismiss";
// game_api::SetNextState arguments the GUI actions use (the game states are 0..6).
enum GameState : int { StateExitGame = 1, StateResetGame = 4, StateStartGame = 5, StateWonGame = 6 };
inline constexpr const char* kExitConfirmDialog = "ExitConfirmBox";
inline constexpr const char* kNoneMenu = "NONE"; // BuildingDef::m_Menu of the helper buildings NextBuilding / PrevBuilding skip

// The staff browser buttons: the StaffBrowser dialog lists one staff type.
constexpr int BrowserStaffType(Action action)
{
    switch (action) {
    case Action::CleanerBrowser: return StaffCleaner;
    case Action::BuilderBrowser: return StaffBuilder;
    case Action::MechanicBrowser: return StaffMechanic;
    case Action::RepBrowser: return StaffRep;
    case Action::LifeguardBrowser: return StaffLifeguard;
    case Action::SecurityBrowser: return StaffSecurity;
    default: return -1;
    }
}

// The *Icon buttons select the next staff member of a class: object class (Entity::m_type) and the slot of the static index
// (g_StaffSelectIndex, one uint per button) gui_manager_buttons::SelectStaffByClass advances.
struct StaffIcon {
    int objectClass;
    int slot;
};
constexpr StaffIcon IconOf(Action action)
{
    switch (action) {
    case Action::CleanerIcon: return {0xb, 0};
    case Action::RepIcon: return {9, 1};
    case Action::LifeguardIcon: return {7, 2};
    case Action::MechanicIcon: return {8, 3};
    case Action::SecurityIcon: return {0xc, 4};
    case Action::BuilderIcon: return {0xa, 5};
    default: return {-1, -1};
    }
}

// HireNewStaff: the home building kind of a staff type and the message keys when none can take a new hire (homes exist but are full or busy ->
// NOSPACE; none exists -> NOHOME). Order of the table = StaffType.
struct HireInfo {
    const char* home;
    const char* noSpace;
    const char* noHome;
};
constexpr HireInfo HireInfoOf(int type)
{
    switch (type) {
    case StaffCleaner: return {"CLEANERSHACK", "STAFFB_NOSPACE_CLEANERS", "STAFFB_NOHOME_CLEANERS"};
    case StaffBuilder: return {"BUILDERYARD", "STAFFB_NOSPACE_BUILDERS", "STAFFB_NOHOME_BUILDERS"};
    case StaffMechanic: return {"HANDYMAN", "STAFFB_NOSPACE_MECHANICS", "STAFFB_NOHOME_MECHANICS"};
    case StaffRep: return {"REP", "STAFFB_NOSPACE_REPS", "STAFFB_NOHOME_REPS"};
    case StaffLifeguard: return {"BAYWATCH", "STAFFB_NOSPACE_LIFEGUARDS", "STAFFB_NOHOME_LIFEGUARDS"};
    case StaffSecurity: return {"SECURITY_POST", "STAFFB_NOSPACE_SECURITY", "STAFFB_NOHOME_SECURITY"};
    default: return {nullptr, nullptr, nullptr};
    }
}

inline constexpr float kPanelAnimationSpeed = 1.0f;
inline constexpr float kDefaultMusicVolume = 0.5f; // what Settings reset restores
inline constexpr float kDefaultEffectsVolume = 0.75f;

// The command of an action that only forwards one to the music player, -1 for the others.
constexpr int PlainMusicCommand(Action action)
{
    switch (action) {
    case Action::MusicSun: return MusicSun;
    case Action::MusicMoon: return MusicMoon;
    case Action::MusicRepeat: return MusicRepeat;
    case Action::MusicShuffle: return MusicShuffle;
    case Action::MusicDelete: return MusicDelete;
    case Action::MusicMoveUp: return MusicMoveUp;
    case Action::MusicMoveDown: return MusicMoveDown;
    case Action::MusicPlay: return MusicPlay;
    case Action::MusicNext: return MusicNext;
    case Action::MusicPrevious: return MusicPrevious;
    case Action::MusicStop: return MusicStop;
    case Action::MusicPause: return MusicPause;
    case Action::MusicDefault: return MusicDefault;
    default: return -1;
    }
}

constexpr float VolumeUp(float volume)
{
    float next = volume + kVolumeStep;
    if (next > 1.0f) next = 1.0f;
    return next;
}
constexpr float VolumeDown(float volume)
{
    float next = volume - kVolumeStep;
    if (!(next >= 0.0f)) next = 0.0f;
    return next;
}

// ToggleSelection: selecting the selected object again clears the selection.
constexpr int ToggledSelection(int selection, int objectId)
{
    return selection == objectId ? -1 : objectId;
}

// Zoom level (0 closest .. 2 farthest) the ZoomIn / ZoomOut button asks for, -1 when the level is already at the end.
constexpr int ZoomInRequest(int level)
{
    return level == 1 ? 0 : level == 2 ? 1 : -1;
}
constexpr int ZoomOutRequest(int level)
{
    return level == 0 ? 1 : level == 1 ? 2 : -1;
}

template<class Env> void TogglePanel(Env& env, Panel panel)
{
    if (!env.PanelEnabled(panel)) return;
    const bool radar = panel == Panel::Radar;
    env.PanelReleaseSprite(panel);
    const char* const action = radar ? "ToggleRadarSize" : "ToggleProgressSize";
    if (env.PanelFlag(panel)) {
        env.PanelSetGraphic(panel, action, radar ? "ui3_scanner_downbutton" : "ui3_progress_upbutton");
        env.PanelCreateSprite(panel, radar ? "ui3_scanner_panelup" : "ui3_progress_panelup");
        env.PanelWriteFlag(panel, false);
        if (!radar) env.PanelInvalidateButton(panel);
    } else {
        env.PanelSetGraphic(panel, action, radar ? "ui3_scanner_upbutton" : "ui3_progress_downbutton");
        env.PanelCreateSprite(panel, radar ? "ui3_scanner_paneldown" : "ui3_progress_paneldown");

        if (radar) {
            env.PanelInvalidateWidget(panel);
            env.PanelWriteFlag(panel, true);
        } else {
            env.PanelWriteFlag(panel, true);
            env.PanelInvalidateWidget(panel);
            env.PanelInvalidateButton(panel);
        }
    }
    if (radar) {
        env.ClearRadarSurface();
        env.PanelInvalidateButton(panel);
    }
    env.PanelWriteDirty(panel);
}

// The ticker history button: toggles the list dialog and swaps the button's graphic to match.
template<class Env> void ToggleTicker(Env& env, const char* dialog, const char* button, const char* openGraphic, const char* closedGraphic)
{
    const bool opened = env.ToggleDialog(dialog);
    env.SetButtonGraphic(button, opened ? openGraphic : closedGraphic);
    env.SetTickerVisible(opened);
}

// Save-game button of the save dialog (SaveGame when the slot is free, SaveGameConfirm after "overwrite?"): writes, then swaps the boxes.
template<class Env> void WriteSaveAndConfirm(Env& env)
{
    env.WriteSave();
    env.CloseDialog("SaveOverBox");
    env.ShowDialog("SaveOKBox", true);
}

// Delete-confirm and "save done" buttons: refresh the list of the dialog that stays behind and redraw it.
template<class Env> void RefreshSaveMenu(Env& env)
{
    env.FillSaveList();
    env.InvalidateDialog("SaveGameMenu");
}

template<class Env> void PlaceBuilding(Env& env, int arg)
{
    const int id = env.BuildingId();
    bool ok = true;
    if (env.BuildingLockedByTree(id)) ok = false;
    if (!env.BuildingAllowed(id)) ok = false;
    if (!(env.Cash() - static_cast<double>(env.BuildingCost(id)) > 0.0) || !ok) return;
    env.CloseDialogById(arg);
    if (env.BuildingHasFootprint(id)) env.WriteRequestedMode(kModePlaceBuilding);
    env.ShowDialog("CancelBuildMode", false);
}

constexpr bool NeedsProvider(Action action)
{
    switch (action) {
    case Action::SellButton:
    case Action::ConfirmSell:
    case Action::BreakObject:
    case Action::CallRepair:
    case Action::CallCleaner:
    case Action::OpenBuilding:
    case Action::CloseBuilding:
    case Action::NextBuilding:
    case Action::PrevBuilding:
    case Action::StaffFire:
    case Action::DismissConfirm:
    case Action::NextStaff:
    case Action::PrevStaff:
    case Action::StaffSend:
    case Action::StaffHome:
    case Action::IncStaffHours:
    case Action::DecStaffHours:
    case Action::SetStaffWages:
    case Action::SetStaffShift: return true;
    default: return false;
    }
}
constexpr bool NeedsBrowsedStaff(Action action)
{
    return action == Action::DismissStaff || action == Action::StaffSetAllWages || action == Action::StaffSetAllShift;
}
constexpr bool NeedsHome(Action action)
{
    return action == Action::StaffHome || action == Action::DismissConfirm;
}

template<class Env> void SelectAdjacentBuilding(Env& env, bool forward)
{
    void* object = env.Provider();
    uint32_t id = env.ObjectId(object);
    do {
        id = env.AdjacentBuilding(id, forward);
        object = env.ObjectById(id);
        id = env.ObjectId(object);
    } while (env.BuildingMenuIsNone(object));
    env.SelectObject(object);
}

// Next / PrevStaff: the neighbouring staff member of the same type; nothing when the provider is the only one.
template<class Env> void SelectAdjacentStaff(Env& env, bool forward)
{
    void* object = env.Provider();
    const uint32_t id = env.ObjectId(object);
    const uint32_t next = env.AdjacentStaff(id, env.StaffClass(object), forward);
    if (next != id) env.SelectObject(env.ObjectById(next));
}

// The staff browser button of a type: toggles the window unless it already lists another type, then points the browser and the hire button at the type.
template<class Env> void ShowStaffBrowser(Env& env, int type)
{
    if (env.IsDialogOpen(kStaffBrowserDialog)) {
        if (env.StaffBrowserType() == type) env.CloseDialog(kStaffBrowserDialog);
    } else {
        env.ShowDialog(kStaffBrowserDialog, false);
    }
    env.SetContext(kContextStaffBrowserType, type);
    env.SetContext(kContextHireType, type);
}

// HireNewStaff: hires into the first home building of the type with a free place and no build / demolish job; the message says why not.
template<class Env> void HireNewStaff(Env& env)
{
    const HireInfo info = HireInfoOf(env.HireType());
    if (!info.home) return;
    const uint32_t capacity = env.HomeCapacity(info.home);
    const auto homes = env.HomeBuildings(info.home);
    for (void* home : homes) {
        if (env.StaffCount(home) >= capacity) continue;
        if (env.HasBuildJob(home) || env.HasDemolishJob(home)) continue;
        env.HireStaff(home);
        env.RefreshAllDialogs();
        return;
    }
    env.ShowMessageBox(homes.empty() ? info.noHome : info.noSpace);
}

// SetStaffWages / SetStaffShift (provider) and StaffSetAllWages / StaffSetAllShift (the browsed member): the source's wage or shift goes to
// every staff member of its class, the source included.
template<class Env> void CopyWageToClass(Env& env, void* source)
{
    const int wage = env.Wage(source);
    for (void* member : env.ObjectsOfClass(env.StaffClass(source))) {
        env.WriteWage(member, wage);
        env.RecalculateHappiness(member);
    }
}
template<class Env> void CopyShiftToClass(Env& env, void* source)
{
    const int start = env.ShiftStart(source), end = env.ShiftEnd(source);
    for (void* member : env.ObjectsOfClass(env.StaffClass(source))) env.SetShift(member, start, end);
}

template<class Env> void Run(Env& env, Action action, int arg = 0)
{
    if (const int command = PlainMusicCommand(action); command >= 0) {
        env.MusicCommand(command);
        return;
    }
    switch (action) {
    case Action::IncDist: env.WriteMaxDistance(env.MaxDistance() + kDistanceStep); break;
    case Action::DecDist: env.WriteMaxDistance(env.MaxDistance() - kDistanceStep); break;
    case Action::CameraMode:
        if (env.IsDialogOpen("CameraControls")) {
            env.CloseDialog("CameraControls");
            env.SetMode(kModeDefault);
            env.WriteRadarEnabled(true);

            if (!env.IsSandbox()) env.WriteProgressEnabled(true);
        } else {
            env.CloseAllDialogs();
            env.ShowDialog("CameraControls", false);
            env.SetMode(kModeCameraControls);
        }
        break;
    case Action::DoCameraGrab: env.WriteCameraGrab(true); break;
    case Action::LockCamera:

        if (env.HasProvider()) env.ToggleSelection(env.ProviderId());
        break;
    case Action::ZoomIn:
        if (const int request = ZoomInRequest(env.ZoomLevel()); request >= 0) env.WriteZoomRequest(request);
        break;
    case Action::ZoomOut:
        if (const int request = ZoomOutRequest(env.ZoomLevel()); request >= 0) env.WriteZoomRequest(request);
        break;
    case Action::ToggleRadarSize: TogglePanel(env, Panel::Radar); break;
    case Action::ToggleProgressSize: TogglePanel(env, Panel::Progress); break;
    case Action::ShowTickerList800:
        ToggleTicker(env, "TickerList800", "ShowTickerList800", "UI3_bar_message_down", "UI3_bar_message_up");
        break;
    case Action::ShowTickerList640:
        ToggleTicker(env, "TickerList640", "ShowTickerList640", "UI2_bar_message_down", "UI2_bar_message_up");
        break;
    case Action::MusicList: env.ToggleMusicPanel(); break;
    case Action::MusicRefresh:
        env.CloseDialog("MP3RefreshConfirm");
        env.ShowDialog("MP3Refreshing", true);
        env.MusicCommand(MusicRefresh);
        break;
    case Action::MusicRefreshConfirm: env.ShowDialog("MP3RefreshConfirm", true); break;
    case Action::MusicVolUp: env.SetMusicVolume(VolumeUp(env.MusicVolume())); break;
    case Action::MusicVolDown: env.SetMusicVolume(VolumeDown(env.MusicVolume())); break;
    case Action::ExitDialog: env.CloseDialogById(arg); break;
    case Action::ExitDialogResume:
        env.CloseDialogById(arg);
        env.CloseDialog("PauseBox");
        env.ResumeGame(); // clears the script's forced pause as well, unlike the pause key
        break;
    case Action::ExitDialogMessage:
        env.CloseDialogById(arg);
        env.PostDialogClosed();
        break;
    case Action::ClosePause:
        env.ClosePauseDialog();
        env.WritePaused(false);
        break;
    case Action::ShowGoal: env.ToggleDialog(env.IsSandbox() ? "SandBoxPanel" : "Objectives"); break;
    case Action::InGameOptions: env.ToggleInGameOptions(); break;
    case Action::ExitOptions:
        env.CloseDialogById(arg);
        env.SaveSettings();
        break;
    case Action::ResetVideoOptions:
        env.WriteVideoDefaults();
        env.ApplySeaEffects(true);
        break;
    case Action::ResetAudioOptions:
        env.WriteSettingsMusicVolume(kDefaultMusicVolume);
        env.WriteSettingsEffectsVolume(kDefaultEffectsVolume);
        env.ApplyMusicVolume(kDefaultMusicVolume);
        env.ApplyEffectsVolume(kDefaultEffectsVolume);
        break;
    case Action::LoadGameMenu:
        env.ScanSaveSlots();
        env.CloseAllDialogs();
        env.ShowDialog("LoadGameMenu", true);
        env.FillLoadList();
        break;
    case Action::LoadGamePanel: env.ToggleLoadMenu(); break;
    case Action::SaveGamePanel: env.ToggleSaveMenu(); break;
    case Action::ExitLoadGame:
        env.ClearSaveSlots();
        if (env.HasWorld()) {
            env.CloseDialog("SaveGameMenu");
            env.CloseDialog("LoadGameMenu");
            env.CloseDialog("SaveOKBox");
        } else {
            env.CloseAllDialogs();
            env.ShowDialog("MainMenu", false);
        }
        break;
    case Action::LoadGame:
        env.RequestLoadSelectedSave();
        env.CloseAllDialogs();
        break;
    case Action::SaveGame:
        if (env.SaveSlotExists())
            env.ShowDialog("SaveOverBox", true);
        else
            WriteSaveAndConfirm(env);
        break;
    case Action::SaveGameConfirm: WriteSaveAndConfirm(env); break;
    case Action::DeleteSaveGame: env.ShowDialog("DeleteSaveConfirmBox", true); break;
    case Action::DeleteLoadGame: env.ShowDialog("DeleteLoadConfirmBox", true); break;
    case Action::DeleteSaveGameConfirm:
        env.DeleteSelectedSave();
        env.CloseDialogById(arg);
        RefreshSaveMenu(env);
        break;
    case Action::DeleteLoadGameConfirm:
        env.DeleteSelectedLoad();
        env.CloseDialogById(arg);
        env.FillLoadList();
        env.InvalidateDialog("LoadGameMenu");
        break;
    case Action::ExitSaveOK:
        env.CloseDialogById(arg);
        env.ScanSaveSlots();
        RefreshSaveMenu(env);
        break;
    case Action::BuildList: env.ToggleBuildPanel(); break;
    case Action::PlaceBuilding: PlaceBuilding(env, arg); break;
    case Action::SaveGridMap: env.SaveGridMap(); break;
    case Action::PaintMap:
        env.CloseAllDialogs();
        env.SetMode(kModePaint);
        break;
    case Action::ExitBuildMode: // StopPaint is the same function
        env.CloseAllDialogs();
        env.SetMode(kModeDefault);
        break;
    case Action::SceneryPanel: env.ToggleSceneryPanel(); break;
    case Action::ExitSceneryPanel:
        env.AnimateDialog("SceneryPanel", 2, 0);
        env.SetMode(kModeDefault);
        break;
    case Action::MinSceneryPanel:
        if (!env.DialogMinimized("SceneryPanel")) {
            env.AnimateDialog("SceneryPanel", 3, 0x9d);
            env.SetDialogMinimized("SceneryPanel", true);
            env.SetButtonGraphic("MinSceneryPanel", "UI3_sc_out");
        } else {
            env.AnimateDialog("SceneryPanel", 4, 0);
            env.SetDialogMinimized("SceneryPanel", false);
            env.SetButtonGraphic("MinSceneryPanel", "UI3_sc_in");
        }
        break;
    case Action::SceneryDozer:
        env.SetMode(kModeSceneryDozer);
        env.FlashButtons("SceneryDozer", true);
        break;
    case Action::ObjectBrowser:
        env.ClearSelectionByClass(kObjectClassGuest);
        env.ToggleDialog("ObjectBrowser");
        break;
    case Action::GuestBrowser:
        env.ClearSelectionByClass(kObjectClassGuest);
        if (env.ToggleDialog("ObjectBrowser")) env.SelectDialogTab("ObjectBrowser", 0);
        break;
    case Action::SellButton: env.ShowDialogFor("ConfirmDemolish", env.Provider(), true); break;
    case Action::ConfirmSell: {
        void* provider = env.Provider();
        env.CloseDialogByProvider(provider);
        env.BeginDemolish(provider);
        break;
    }
    case Action::BreakObject: env.BreakObject(env.Provider()); break;
    case Action::CallRepair: {
        void* provider = env.Provider();
        env.AddUrgentRepair(env.ObjectId(provider));
        env.SetCalledRepairing(provider, true);
        break;
    }
    case Action::CallCleaner: {
        void* provider = env.Provider();
        env.AddUrgentCleaning(env.ObjectId(provider));
        env.SetCalledCleaning(provider, true);
        break;
    }
    case Action::OpenBuilding: env.OpenBuilding(env.Provider()); break;
    case Action::CloseBuilding: env.CloseBuilding(env.Provider()); break;
    case Action::NextBuilding: SelectAdjacentBuilding(env, true); break;
    case Action::PrevBuilding: SelectAdjacentBuilding(env, false); break;
    case Action::PowerPanel: env.ToggleDialog("PowerPanel"); break;
    case Action::MoneyPanel: env.ToggleDialog("MoneyPanel"); break;
    case Action::IslandStatus: env.ToggleDialog("IslandStatus"); break;
    case Action::FerryPanel: env.ToggleDialog("FerryPanel"); break;
    case Action::CleanerBrowser:
    case Action::BuilderBrowser:
    case Action::MechanicBrowser:
    case Action::RepBrowser:
    case Action::LifeguardBrowser:
    case Action::SecurityBrowser: ShowStaffBrowser(env, BrowserStaffType(action)); break;
    case Action::HireNewStaff: HireNewStaff(env); break;
    case Action::DismissStaff:
        env.ShowDialogFor(kConfirmDismissDialog, env.ObjectById(static_cast<uint32_t>(env.BrowsedStaffId())), true);
        break;
    case Action::StaffFire: env.ShowDialogFor(kConfirmDismissDialog, env.Provider(), true); break;
    case Action::DismissConfirm: {
        void* provider = env.Provider();
        env.DismissStaff(env.ObjectById(env.HomeId(provider)), env.ObjectId(provider));
        env.CloseDialogById(arg);
        env.CloseDialogByProvider(provider);
        env.RefreshAllDialogs();
        break;
    }
    case Action::NextStaff: SelectAdjacentStaff(env, true); break;
    case Action::PrevStaff: SelectAdjacentStaff(env, false); break;
    case Action::StaffSend:
        env.CloseAllDialogs();
        env.SetContext(kContextSendStaff, static_cast<int>(env.ObjectId(env.Provider())));
        env.SetMode(kModeStaffSend);
        break;
    case Action::StaffHome: env.SelectObject(env.ObjectById(env.HomeId(env.Provider()))); break;
    case Action::IncStaffHours: env.IncShift(env.Provider()); break;
    case Action::DecStaffHours: env.DecShift(env.Provider()); break;
    case Action::SetStaffWages: CopyWageToClass(env, env.Provider()); break;
    case Action::SetStaffShift: CopyShiftToClass(env, env.Provider()); break;
    case Action::StaffSetAllWages: CopyWageToClass(env, env.ObjectById(static_cast<uint32_t>(env.BrowsedStaffId()))); break;
    case Action::StaffSetAllShift: CopyShiftToClass(env, env.ObjectById(static_cast<uint32_t>(env.BrowsedStaffId()))); break;
    case Action::CleanerIcon:
    case Action::BuilderIcon:
    case Action::MechanicIcon:
    case Action::RepIcon:
    case Action::LifeguardIcon:
    case Action::SecurityIcon: {
        const StaffIcon icon = IconOf(action);

        if (env.HasStaffOfClass(icon.objectClass)) env.ToggleSelection(env.SelectStaffByClass(icon.objectClass, icon.slot));
        break;
    }
    case Action::None: break;
    case Action::StartNewGame:

        env.ShowDialog(env.Rand() % 2 == 0 ? "LoadingScreen2" : "LoadingScreen1", false);
        env.SetNextState(StateStartGame);
        break;

    case Action::ExitWindows: env.ShowDialogFor(kExitConfirmDialog, env.Provider(), true); break;
    case Action::ExitWindowsConfirm: env.PostQuit(); break;
    case Action::ShowMainMenu:
        env.CloseAllDialogs();
        env.ShowDialog("MainMenu", false);
        break;
    case Action::ExtrasMenu:
        env.CloseAllDialogs();
        env.ShowDialog("ExtrasScreen", false);
        env.DestroyCredits();
        break;
    case Action::ShowScenSel:
    case Action::PlaySandBox:
        env.SetSandboxMode(action == Action::PlaySandBox);
        env.CloseAllDialogs();
        env.ShowDialog("ScenarioScreen", false);
        break;
    case Action::ResetGame: env.ShowDialog("ResetConfirmBox", true); break;
    case Action::ResetGameConfirm: env.SetNextState(StateResetGame); break;
    case Action::ExitGame: env.ShowDialog(kExitConfirmDialog, true); break;
    case Action::ExitGameConfirm: env.SetNextState(StateExitGame); break;
    case Action::WonGame: env.SetNextState(StateWonGame); break;
    case Action::CreditsMenu:
        env.CloseAllDialogs();
        env.ShowDialog("CreditsScreen", false);
        env.CreateCredits();
        break;
    case Action::ResetProgress: env.ShowDialog("ResetProgressConfirm", true); break;
    case Action::ResetProgressConfirm:
        env.CloseDialogById(arg);
        env.ResetScenarioProgress();
        env.SaveScenarioProgress();
        break;
    default: break;
    }
}

} // namespace obl::gui_actions
