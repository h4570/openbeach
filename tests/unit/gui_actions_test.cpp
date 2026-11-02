#include "unit_test.h"
#include "game/ui/gui_actions.h"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <limits>
#include <string>
#include <vector>

using namespace obl::gui_actions;
namespace {
// Records every effect as text; queries answer from public fields.
struct Env {
    std::vector<std::string> log;
    float maxDistance = 100, musicVolume = 0.5f;
    int zoomLevel = 1, selection = -1, providerId = 7, openDialogs = 0;
    bool hasProvider = true, sandbox = false, radarEnabled = true, radarFlag = false, progressEnabled = true, progressFlag = false;
    bool dialogOpen = false, toggleResult = true;

    float MaxDistance() { return maxDistance; }
    void WriteMaxDistance(float v) { log.push_back("dist=" + std::to_string(v)); }
    int ZoomLevel() { return zoomLevel; }
    void WriteZoomRequest(int v) { log.push_back("zoom=" + std::to_string(v)); }
    void WriteCameraGrab(bool v) { log.push_back(std::string("grab=") + (v ? "1" : "0")); }
    bool HasProvider() { return hasProvider; }
    int ProviderId() { return providerId; }
    int Selection() { return selection; }
    void ToggleSelection(int id) { log.push_back("selection=" + std::to_string(ToggledSelection(selection, id))); }
    bool IsDialogOpen(const char*) { return dialogOpen; }
    void CloseDialog(const char* n) { log.push_back(std::string("close ") + n); }
    void ShowDialog(const char* n, bool modal) { log.push_back(std::string("show ") + n + (modal ? " modal" : "")); }
    void CloseAllDialogs() { log.push_back("closeall"); }
    bool ToggleDialog(const char* n)
    {
        log.push_back(std::string("toggle ") + n);
        return toggleResult;
    }
    void SetMode(int m) { log.push_back("mode " + std::to_string(m)); }
    bool IsSandbox() { return sandbox; }
    void SetButtonGraphic(const char* b, const char* g) { log.push_back(std::string("button ") + b + " " + g); }
    void SetTickerVisible(bool v) { log.push_back(std::string("ticker ") + (v ? "1" : "0")); }
    void ToggleMusicPanel() { log.push_back("musicpanel"); }
    void MusicCommand(int c) { log.push_back("music " + std::to_string(c)); }
    float MusicVolume() { return musicVolume; }
    void SetMusicVolume(float v) { log.push_back("volume " + (std::isnan(v) ? std::string("nan") : std::to_string(v))); }
    bool PanelEnabled(Panel p) { return p == Panel::Radar ? radarEnabled : progressEnabled; }
    bool PanelFlag(Panel p) { return p == Panel::Radar ? radarFlag : progressFlag; }
    void PanelWriteFlag(Panel, bool v) { log.push_back(std::string("flag=") + (v ? "1" : "0")); }
    void PanelWriteDirty(Panel) { log.push_back("dirty"); }
    void PanelReleaseSprite(Panel) { log.push_back("release"); }
    void PanelCreateSprite(Panel, const char* n) { log.push_back(std::string("sprite ") + n); }
    void PanelSetGraphic(Panel, const char* a, const char* g) { log.push_back(std::string("graphic ") + a + " " + g); }
    void PanelInvalidateWidget(Panel) { log.push_back("invalidate widget"); }
    void PanelInvalidateButton(Panel) { log.push_back("invalidate button"); }
    void ClearRadarSurface() { log.push_back("clear"); }
    void WriteRadarEnabled(bool v) { log.push_back(std::string("radar=") + (v ? "1" : "0")); }
    void WriteProgressEnabled(bool v) { log.push_back(std::string("progress=") + (v ? "1" : "0")); }
    void CloseDialogById(int id) { log.push_back("closeid " + std::to_string(id)); }
    void PostDialogClosed() { log.push_back("post dialog closed"); }
    void ClosePauseDialog() { log.push_back("close pause dialog"); }
    void WritePaused(bool v) { log.push_back(std::string("paused=") + (v ? "1" : "0")); }
    void ResumeGame() { log.push_back("resume"); }
    void ToggleInGameOptions() { log.push_back("ingame options"); }
    void SaveSettings() { log.push_back("save settings"); }
    void WriteVideoDefaults() { log.push_back("video defaults"); }
    void ApplySeaEffects(bool v) { log.push_back(std::string("sea ") + (v ? "1" : "0")); }
    void WriteSettingsMusicVolume(float v) { log.push_back("settings music " + std::to_string(v)); }
    void WriteSettingsEffectsVolume(float v) { log.push_back("settings effects " + std::to_string(v)); }
    void ApplyMusicVolume(float v) { log.push_back("apply music " + std::to_string(v)); }
    void ApplyEffectsVolume(float v) { log.push_back("apply effects " + std::to_string(v)); }

    bool hasWorld = true, slotExists = false, minimized = false, locked = false, allowed = true, footprint = true;
    int buildingId = 3, cost = 100;
    double cash = 500;
    bool HasWorld() { return hasWorld; }
    void InvalidateDialog(const char* n) { log.push_back(std::string("invalidate ") + n); }
    void ScanSaveSlots() { log.push_back("scan"); }
    void ClearSaveSlots() { log.push_back("clear slots"); }
    void FillLoadList() { log.push_back("fill load"); }
    void FillSaveList() { log.push_back("fill save"); }
    bool SaveSlotExists() { return slotExists; }
    void WriteSave() { log.push_back("write save"); }
    void RequestLoadSelectedSave() { log.push_back("request load"); }
    void DeleteSelectedSave() { log.push_back("delete save"); }
    void DeleteSelectedLoad() { log.push_back("delete load"); }
    void ToggleLoadMenu() { log.push_back("toggle load menu"); }
    void ToggleSaveMenu() { log.push_back("toggle save menu"); }
    void ToggleBuildPanel() { log.push_back("toggle build"); }
    void ToggleSceneryPanel() { log.push_back("toggle scenery"); }
    void AnimateDialog(const char* n, int type, int delay)
    {
        log.push_back(std::string("animate ") + n + " " + std::to_string(type) + " " + std::to_string(delay));
    }
    bool DialogMinimized(const char*) { return minimized; }
    void SetDialogMinimized(const char* n, bool v) { log.push_back(std::string("minimized ") + n + (v ? " 1" : " 0")); }
    void FlashButtons(const char* a, bool v) { log.push_back(std::string("flash ") + a + (v ? " 1" : " 0")); }
    void SaveGridMap() { log.push_back("grid map"); }
    int BuildingId() { return buildingId; }
    bool BuildingLockedByTree(int) { return locked; }
    bool BuildingAllowed(int) { return allowed; }
    double Cash() { return cash; }
    int BuildingCost(int) { return cost; }
    bool BuildingHasFootprint(int) { return footprint; }
    void WriteRequestedMode(int m) { log.push_back("requested mode " + std::to_string(m)); }

    static void* Obj(uintptr_t id) { return reinterpret_cast<void*>(id); }
    static uint32_t Id(void* o) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o)); }
    int staffBrowserType = 0, hireType = 0, browsedStaff = 40, staffClass = 9;
    uint32_t capacity = 2, homeStaff = 0;
    std::vector<uint32_t> homes, busyHomes, noneBuildings;
    bool hasStaff = true;
    void* Provider() { return Obj(static_cast<uintptr_t>(providerId)); }
    uint32_t ObjectId(void* o) { return Id(o); }
    void* ObjectById(uint32_t id) { return Obj(id); }
    void ClearSelectionByClass(int c) { log.push_back("clear class " + std::to_string(c)); }
    void SelectDialogTab(const char* n, int t) { log.push_back(std::string("tab ") + n + " " + std::to_string(t)); }
    void ShowDialogFor(const char* n, void* o, bool modal)
    {
        log.push_back(std::string("show ") + n + " for " + std::to_string(Id(o)) + (modal ? " modal" : ""));
    }
    void CloseDialogByProvider(void* o) { log.push_back("close for " + std::to_string(Id(o))); }
    void BeginDemolish(void* o) { log.push_back("demolish " + std::to_string(Id(o))); }
    void OpenBuilding(void* o) { log.push_back("open " + std::to_string(Id(o))); }
    void CloseBuilding(void* o) { log.push_back("close building " + std::to_string(Id(o))); }
    void BreakObject(void* o) { log.push_back("break " + std::to_string(Id(o))); }
    void AddUrgentRepair(uint32_t id) { log.push_back("urgent repair " + std::to_string(id)); }
    void AddUrgentCleaning(uint32_t id) { log.push_back("urgent cleaning " + std::to_string(id)); }
    void SetCalledRepairing(void* o, bool v) { log.push_back("called repairing " + std::to_string(Id(o)) + (v ? " 1" : " 0")); }
    void SetCalledCleaning(void* o, bool v) { log.push_back("called cleaning " + std::to_string(Id(o)) + (v ? " 1" : " 0")); }
    uint32_t AdjacentBuilding(uint32_t id, bool forward) { return forward ? id + 1 : id - 1; }
    bool BuildingMenuIsNone(void* o) { return std::find(noneBuildings.begin(), noneBuildings.end(), Id(o)) != noneBuildings.end(); }
    void SelectObject(void* o) { log.push_back("select " + std::to_string(Id(o))); }
    int StaffBrowserType() { return staffBrowserType; }
    int HireType() { return hireType; }
    int BrowsedStaffId() { return browsedStaff; }
    void SetContext(int i, int v) { log.push_back("context " + std::to_string(i) + " " + std::to_string(v)); }
    int StaffClass(void*) { return staffClass; }
    uint32_t AdjacentStaff(uint32_t id, int, bool forward) { return forward ? id + 1 : id; }
    uint32_t HomeId(void*) { return 77; }
    void IncShift(void* o) { log.push_back("inc shift " + std::to_string(Id(o))); }
    void DecShift(void* o) { log.push_back("dec shift " + std::to_string(Id(o))); }
    bool HasStaffOfClass(int) { return hasStaff; }
    uint32_t SelectStaffByClass(int c, int slot)
    {
        log.push_back("select staff " + std::to_string(c) + " " + std::to_string(slot));
        return 99;
    }
    std::vector<void*> ObjectsOfClass(int c)
    {
        log.push_back("class " + std::to_string(c));
        return {Obj(21), Obj(22)};
    }
    std::vector<void*> HomeBuildings(const char* name)
    {
        log.push_back(std::string("homes ") + name);
        std::vector<void*> result;
        for (uint32_t id : homes) result.push_back(Obj(id));
        return result;
    }
    int Wage(void*) { return 55; }
    void WriteWage(void* o, int w) { log.push_back("wage " + std::to_string(Id(o)) + " " + std::to_string(w)); }
    void RecalculateHappiness(void* o) { log.push_back("happiness " + std::to_string(Id(o))); }
    int ShiftStart(void*) { return 8; }
    int ShiftEnd(void*) { return 16; }
    void SetShift(void* o, int s, int e)
    {
        log.push_back("shift " + std::to_string(Id(o)) + " " + std::to_string(s) + "-" + std::to_string(e));
    }
    uint32_t HomeCapacity(const char*) { return capacity; }
    uint32_t StaffCount(void*) { return homeStaff; }
    bool HasBuildJob(void* o) { return std::find(busyHomes.begin(), busyHomes.end(), Id(o)) != busyHomes.end(); }
    bool HasDemolishJob(void*) { return false; }
    void HireStaff(void* o) { log.push_back("hire " + std::to_string(Id(o))); }
    void DismissStaff(void* home, uint32_t staff) { log.push_back("dismiss " + std::to_string(Id(home)) + " " + std::to_string(staff)); }
    void RefreshAllDialogs() { log.push_back("refresh"); }
    void ShowMessageBox(const char* key) { log.push_back(std::string("message ") + key); }

    int rand = 0;
    void SetNextState(int s) { log.push_back("state " + std::to_string(s)); }
    void SetSandboxMode(bool v) { log.push_back(std::string("sandbox ") + (v ? "1" : "0")); }
    void ResetScenarioProgress() { log.push_back("reset progress"); }
    void SaveScenarioProgress() { log.push_back("save progress"); }
    void PostQuit() { log.push_back("quit"); }
    int Rand()
    {
        log.push_back("rand");
        return rand;
    }
    void CreateCredits() { log.push_back("create credits"); }
    void DestroyCredits() { log.push_back("destroy credits"); }
};

struct Log : std::vector<std::string> {
    using std::vector<std::string>::vector;
    Log(std::vector<std::string> lines) : std::vector<std::string>(std::move(lines)) {}
};
std::ostream& operator<<(std::ostream& out, const Log& log)
{
    for (const auto& line : log) out << '[' << line << ']';
    return out << "(" << log.size() << ")";
}
Log Do(Action action, Env env = {}, int arg = 0)
{
    Run(env, action, arg);
    return env.log;
}
} // namespace

OBL_TEST_CASE("gui_actions.music_commands_match_the_player_switch")
{
    OBL_CHECK_EQ(Do(Action::MusicSun), Log{"music 0"});
    OBL_CHECK_EQ(Do(Action::MusicMoon), Log{"music 1"});
    OBL_CHECK_EQ(Do(Action::MusicRepeat), Log{"music 2"});
    OBL_CHECK_EQ(Do(Action::MusicShuffle), Log{"music 3"});
    OBL_CHECK_EQ(Do(Action::MusicDelete), Log{"music 4"});
    OBL_CHECK_EQ(Do(Action::MusicMoveUp), Log{"music 5"});
    OBL_CHECK_EQ(Do(Action::MusicMoveDown), Log{"music 6"});
    OBL_CHECK_EQ(Do(Action::MusicPlay), Log{"music 7"});
    OBL_CHECK_EQ(Do(Action::MusicNext), Log{"music 10"});
    OBL_CHECK_EQ(Do(Action::MusicPrevious), Log{"music 11"});
    OBL_CHECK_EQ(Do(Action::MusicStop), Log{"music 12"});
    OBL_CHECK_EQ(Do(Action::MusicPause), Log{"music 13"});
    OBL_CHECK_EQ(Do(Action::MusicDefault), Log{"music 15"});
    OBL_CHECK_EQ(Do(Action::MusicList), Log{"musicpanel"});
}

OBL_TEST_CASE("gui_actions.music_refresh_dialogs")
{
    OBL_CHECK_EQ(Do(Action::MusicRefresh), (Log{"close MP3RefreshConfirm", "show MP3Refreshing modal", "music 14"}));
    OBL_CHECK_EQ(Do(Action::MusicRefreshConfirm), Log{"show MP3RefreshConfirm modal"});
}

OBL_TEST_CASE("gui_actions.volume_steps_clamp")
{
    OBL_CHECK_EQ(VolumeUp(0.5f), 0.6f);
    OBL_CHECK_EQ(VolumeUp(0.95f), 1.0f);
    OBL_CHECK_EQ(VolumeUp(1.0f), 1.0f);
    OBL_CHECK(std::isnan(VolumeUp(std::numeric_limits<float>::quiet_NaN())));
    OBL_CHECK_EQ(VolumeDown(0.5f), 0.4f);
    OBL_CHECK_EQ(VolumeDown(0.05f), 0.0f);
    OBL_CHECK_EQ(VolumeDown(std::numeric_limits<float>::quiet_NaN()), 0.0f);
    Env env;
    env.musicVolume = 0.95f;
    OBL_CHECK_EQ(Do(Action::MusicVolUp, env), Log{"volume 1.000000"});
    env.musicVolume = 0.0f;
    OBL_CHECK_EQ(Do(Action::MusicVolDown, env), Log{"volume 0.000000"});
}

OBL_TEST_CASE("gui_actions.sound_distance_steps_by_five")
{
    Env env;
    env.maxDistance = 100;
    OBL_CHECK_EQ(Do(Action::IncDist, env), Log{"dist=105.000000"});
    OBL_CHECK_EQ(Do(Action::DecDist, env), Log{"dist=95.000000"});
}

OBL_TEST_CASE("gui_actions.zoom_requests")
{
    OBL_CHECK_EQ(ZoomInRequest(0), -1);
    OBL_CHECK_EQ(ZoomInRequest(1), 0);
    OBL_CHECK_EQ(ZoomInRequest(2), 1);
    OBL_CHECK_EQ(ZoomOutRequest(0), 1);
    OBL_CHECK_EQ(ZoomOutRequest(1), 2);
    OBL_CHECK_EQ(ZoomOutRequest(2), -1);
    Env env;
    env.zoomLevel = 2;
    OBL_CHECK_EQ(Do(Action::ZoomIn, env), Log{"zoom=1"});
    OBL_CHECK_EQ(Do(Action::ZoomOut, env), Log{});
    env.zoomLevel = 5;
    OBL_CHECK_EQ(Do(Action::ZoomIn, env), Log{});
    OBL_CHECK_EQ(Do(Action::ZoomOut, env), Log{});
}

OBL_TEST_CASE("gui_actions.camera_mode_opens_and_closes")
{
    Env env;
    OBL_CHECK_EQ(Do(Action::CameraMode, env), (Log{"closeall", "show CameraControls", "mode 2"}));
    env.dialogOpen = true;
    OBL_CHECK_EQ(Do(Action::CameraMode, env), (Log{"close CameraControls", "mode 4", "radar=1", "progress=1"}));
    env.sandbox = true;
    OBL_CHECK_EQ(Do(Action::CameraMode, env), (Log{"close CameraControls", "mode 4", "radar=1"}));
    OBL_CHECK_EQ(Do(Action::DoCameraGrab), Log{"grab=1"});
}

OBL_TEST_CASE("gui_actions.lock_camera_toggles_the_selection")
{
    Env env;
    OBL_CHECK_EQ(Do(Action::LockCamera, env), Log{"selection=7"});
    env.selection = 7;
    OBL_CHECK_EQ(Do(Action::LockCamera, env), Log{"selection=-1"});
    env.selection = 3;
    OBL_CHECK_EQ(Do(Action::LockCamera, env), Log{"selection=7"});
    env.hasProvider = false;
    OBL_CHECK_EQ(Do(Action::LockCamera, env), Log{});
}

OBL_TEST_CASE("gui_actions.ticker_button_follows_the_dialog")
{
    Env env;
    OBL_CHECK_EQ(
            Do(Action::ShowTickerList800, env), (Log{"toggle TickerList800", "button ShowTickerList800 UI3_bar_message_down", "ticker 1"}));
    env.toggleResult = false;
    OBL_CHECK_EQ(
            Do(Action::ShowTickerList640, env), (Log{"toggle TickerList640", "button ShowTickerList640 UI2_bar_message_up", "ticker 0"}));
}

OBL_TEST_CASE("gui_actions.radar_toggle_alternates_graphics")
{
    Env env;
    env.radarEnabled = false;
    OBL_CHECK_EQ(Do(Action::ToggleRadarSize, env), Log{});
    env.radarEnabled = true;
    OBL_CHECK_EQ(Do(Action::ToggleRadarSize, env),
            (Log{"release", "graphic ToggleRadarSize ui3_scanner_upbutton", "sprite ui3_scanner_paneldown", "invalidate widget", "flag=1",
                    "clear", "invalidate button", "dirty"}));
    env.radarFlag = true;
    OBL_CHECK_EQ(Do(Action::ToggleRadarSize, env),
            (Log{"release", "graphic ToggleRadarSize ui3_scanner_downbutton", "sprite ui3_scanner_panelup", "flag=0", "clear",
                    "invalidate button", "dirty"}));
}

OBL_TEST_CASE("gui_actions.progress_toggle_alternates_graphics")
{
    Env env;
    env.progressEnabled = false;
    OBL_CHECK_EQ(Do(Action::ToggleProgressSize, env), Log{});
    env.progressEnabled = true;
    OBL_CHECK_EQ(Do(Action::ToggleProgressSize, env),
            (Log{"release", "graphic ToggleProgressSize ui3_progress_downbutton", "sprite ui3_progress_paneldown", "flag=1",
                    "invalidate widget", "invalidate button", "dirty"}));
    env.progressFlag = true;
    OBL_CHECK_EQ(Do(Action::ToggleProgressSize, env),
            (Log{"release", "graphic ToggleProgressSize ui3_progress_upbutton", "sprite ui3_progress_panelup", "flag=0",
                    "invalidate button", "dirty"}));
}

OBL_TEST_CASE("gui_actions.dialog_closers_pass_the_button_argument")
{
    OBL_CHECK_EQ(Do(Action::ExitDialog, Env{}, 4), Log{"closeid 4"});
    OBL_CHECK_EQ(Do(Action::ExitDialogResume, Env{}, 3), (Log{"closeid 3", "close PauseBox", "resume"}));
    OBL_CHECK_EQ(Do(Action::ExitDialogMessage, Env{}, 9), (Log{"closeid 9", "post dialog closed"}));
    OBL_CHECK_EQ(Do(Action::ExitOptions, Env{}, 2), (Log{"closeid 2", "save settings"}));
    OBL_CHECK_EQ(Do(Action::ClosePause), (Log{"close pause dialog", "paused=0"}));
    OBL_CHECK_EQ(Do(Action::InGameOptions), Log{"ingame options"});
}

OBL_TEST_CASE("gui_actions.show_goal_picks_the_sandbox_panel_in_sandbox")
{
    Env sandbox;
    sandbox.sandbox = true;
    OBL_CHECK_EQ(Do(Action::ShowGoal, sandbox), Log{"toggle SandBoxPanel"});
    OBL_CHECK_EQ(Do(Action::ShowGoal), Log{"toggle Objectives"});
}

OBL_TEST_CASE("gui_actions.load_game_menu_scans_then_lists")
{
    OBL_CHECK_EQ(Do(Action::LoadGameMenu), (Log{"scan", "closeall", "show LoadGameMenu modal", "fill load"}));
    OBL_CHECK_EQ(Do(Action::LoadGamePanel), Log{"toggle load menu"});
    OBL_CHECK_EQ(Do(Action::SaveGamePanel), Log{"toggle save menu"});
    OBL_CHECK_EQ(Do(Action::LoadGame), (Log{"request load", "closeall"}));
}

OBL_TEST_CASE("gui_actions.exit_load_game_depends_on_a_running_world")
{
    OBL_CHECK_EQ(Do(Action::ExitLoadGame), (Log{"clear slots", "close SaveGameMenu", "close LoadGameMenu", "close SaveOKBox"}));
    Env menu;
    menu.hasWorld = false;
    OBL_CHECK_EQ(Do(Action::ExitLoadGame, menu), (Log{"clear slots", "closeall", "show MainMenu"}));
}

OBL_TEST_CASE("gui_actions.save_game_asks_before_overwriting")
{
    Env env;
    OBL_CHECK_EQ(Do(Action::SaveGame, env), (Log{"write save", "close SaveOverBox", "show SaveOKBox modal"}));
    env.slotExists = true;
    OBL_CHECK_EQ(Do(Action::SaveGame, env), Log{"show SaveOverBox modal"});
    OBL_CHECK_EQ(Do(Action::SaveGameConfirm, env), (Log{"write save", "close SaveOverBox", "show SaveOKBox modal"}));
}

OBL_TEST_CASE("gui_actions.save_and_load_confirmations")
{
    OBL_CHECK_EQ(Do(Action::DeleteSaveGame), Log{"show DeleteSaveConfirmBox modal"});
    OBL_CHECK_EQ(Do(Action::DeleteLoadGame), Log{"show DeleteLoadConfirmBox modal"});
    OBL_CHECK_EQ(Do(Action::DeleteSaveGameConfirm, Env{}, 6), (Log{"delete save", "closeid 6", "fill save", "invalidate SaveGameMenu"}));
    OBL_CHECK_EQ(Do(Action::DeleteLoadGameConfirm, Env{}, 7), (Log{"delete load", "closeid 7", "fill load", "invalidate LoadGameMenu"}));
    OBL_CHECK_EQ(Do(Action::ExitSaveOK, Env{}, 8), (Log{"closeid 8", "scan", "fill save", "invalidate SaveGameMenu"}));
}

OBL_TEST_CASE("gui_actions.place_building_needs_unlocked_allowed_affordable")
{
    Env env;
    OBL_CHECK_EQ(Do(Action::PlaceBuilding, env, 5), (Log{"closeid 5", "requested mode 5", "show CancelBuildMode"}));
    env.footprint = false;
    OBL_CHECK_EQ(Do(Action::PlaceBuilding, env, 5), (Log{"closeid 5", "show CancelBuildMode"}));
    env.cash = 100;
    OBL_CHECK_EQ(Do(Action::PlaceBuilding, env, 5), Log{});
    env.cash = 101;
    env.locked = true;
    OBL_CHECK_EQ(Do(Action::PlaceBuilding, env, 5), Log{});
    env.locked = false;
    env.allowed = false;
    OBL_CHECK_EQ(Do(Action::PlaceBuilding, env, 5), Log{});
    env.allowed = true;
    env.cash = std::numeric_limits<double>::quiet_NaN();
    OBL_CHECK_EQ(Do(Action::PlaceBuilding, env, 5), Log{});
}

OBL_TEST_CASE("gui_actions.build_and_scenery_modes")
{
    OBL_CHECK_EQ(Do(Action::BuildList), Log{"toggle build"});
    OBL_CHECK_EQ(Do(Action::SaveGridMap), Log{"grid map"});
    OBL_CHECK_EQ(Do(Action::PaintMap), (Log{"closeall", "mode 0"}));
    OBL_CHECK_EQ(Do(Action::ExitBuildMode), (Log{"closeall", "mode 4"}));
    OBL_CHECK_EQ(Do(Action::SceneryPanel), Log{"toggle scenery"});
    OBL_CHECK_EQ(Do(Action::ExitSceneryPanel), (Log{"animate SceneryPanel 2 0", "mode 4"}));
    OBL_CHECK_EQ(Do(Action::SceneryDozer), (Log{"mode 10", "flash SceneryDozer 1"}));
}

OBL_TEST_CASE("gui_actions.scenery_panel_minimize_toggles")
{
    Env env;
    OBL_CHECK_EQ(Do(Action::MinSceneryPanel, env),
            (Log{"animate SceneryPanel 3 157", "minimized SceneryPanel 1", "button MinSceneryPanel UI3_sc_out"}));
    env.minimized = true;
    OBL_CHECK_EQ(Do(Action::MinSceneryPanel, env),
            (Log{"animate SceneryPanel 4 0", "minimized SceneryPanel 0", "button MinSceneryPanel UI3_sc_in"}));
}

OBL_TEST_CASE("gui_actions.option_resets_restore_the_defaults_before_applying_them")
{
    OBL_CHECK_EQ(Do(Action::ResetVideoOptions), (Log{"video defaults", "sea 1"}));
    OBL_CHECK_EQ(Do(Action::ResetAudioOptions),
            (Log{"settings music " + std::to_string(0.5f), "settings effects " + std::to_string(0.75f),
                    "apply music " + std::to_string(0.5f), "apply effects " + std::to_string(0.75f)}));
}

OBL_TEST_CASE("gui_actions.object_and_guest_browsers_clear_the_guest_selection")
{
    OBL_CHECK_EQ(Do(Action::ObjectBrowser), (Log{"clear class 3", "toggle ObjectBrowser"}));
    Env opened;
    opened.toggleResult = true;
    OBL_CHECK_EQ(Do(Action::GuestBrowser, opened), (Log{"clear class 3", "toggle ObjectBrowser", "tab ObjectBrowser 0"}));
    Env closed;
    closed.toggleResult = false;
    OBL_CHECK_EQ(Do(Action::GuestBrowser, closed), (Log{"clear class 3", "toggle ObjectBrowser"}));
    OBL_CHECK_EQ(Do(Action::PowerPanel), Log{"toggle PowerPanel"});
    OBL_CHECK_EQ(Do(Action::MoneyPanel), Log{"toggle MoneyPanel"});
    OBL_CHECK_EQ(Do(Action::IslandStatus), Log{"toggle IslandStatus"});
    OBL_CHECK_EQ(Do(Action::FerryPanel), Log{"toggle FerryPanel"});
}

OBL_TEST_CASE("gui_actions.building_window_buttons_act_on_the_provider")
{
    OBL_CHECK_EQ(Do(Action::SellButton), Log{"show ConfirmDemolish for 7 modal"});
    OBL_CHECK_EQ(Do(Action::ConfirmSell), (Log{"close for 7", "demolish 7"}));
    OBL_CHECK_EQ(Do(Action::BreakObject), Log{"break 7"});
    OBL_CHECK_EQ(Do(Action::CallRepair), (Log{"urgent repair 7", "called repairing 7 1"}));
    OBL_CHECK_EQ(Do(Action::CallCleaner), (Log{"urgent cleaning 7", "called cleaning 7 1"}));
    OBL_CHECK_EQ(Do(Action::OpenBuilding), Log{"open 7"});
    OBL_CHECK_EQ(Do(Action::CloseBuilding), Log{"close building 7"});
}

OBL_TEST_CASE("gui_actions.next_and_previous_building_skip_the_none_menu")
{
    Env env;
    OBL_CHECK_EQ(Do(Action::NextBuilding, env), Log{"select 8"});
    OBL_CHECK_EQ(Do(Action::PrevBuilding, env), Log{"select 6"});
    env.noneBuildings = {8, 9};
    OBL_CHECK_EQ(Do(Action::NextBuilding, env), Log{"select 10"});
    env.noneBuildings = {6};
    OBL_CHECK_EQ(Do(Action::PrevBuilding, env), Log{"select 5"});
}

OBL_TEST_CASE("gui_actions.staff_browser_toggles_only_for_its_own_type")
{
    Env closed;
    OBL_CHECK_EQ(Do(Action::RepBrowser, closed), (Log{"show StaffBrowser", "context 10 3", "context 11 3"}));
    Env same;
    same.dialogOpen = true;
    same.staffBrowserType = 4;
    OBL_CHECK_EQ(Do(Action::LifeguardBrowser, same), (Log{"close StaffBrowser", "context 10 4", "context 11 4"}));
    Env other;
    other.dialogOpen = true;
    other.staffBrowserType = 0;
    OBL_CHECK_EQ(Do(Action::SecurityBrowser, other), (Log{"context 10 5", "context 11 5"}));
    OBL_CHECK_EQ(Do(Action::CleanerBrowser, closed), (Log{"show StaffBrowser", "context 10 0", "context 11 0"}));
    OBL_CHECK_EQ(Do(Action::BuilderBrowser, closed), (Log{"show StaffBrowser", "context 10 1", "context 11 1"}));
    OBL_CHECK_EQ(Do(Action::MechanicBrowser, closed), (Log{"show StaffBrowser", "context 10 2", "context 11 2"}));
}

OBL_TEST_CASE("gui_actions.hire_new_staff_picks_a_free_home_or_explains")
{
    Env none;
    none.hireType = 2;
    OBL_CHECK_EQ(Do(Action::HireNewStaff, none), (Log{"homes HANDYMAN", "message STAFFB_NOHOME_MECHANICS"}));
    Env full;
    full.hireType = 5;
    full.homes = {30, 31};
    full.homeStaff = 2;
    OBL_CHECK_EQ(Do(Action::HireNewStaff, full), (Log{"homes SECURITY_POST", "message STAFFB_NOSPACE_SECURITY"}));
    Env free;
    free.hireType = 0;
    free.homes = {30, 31};
    free.busyHomes = {30};
    OBL_CHECK_EQ(Do(Action::HireNewStaff, free), (Log{"homes CLEANERSHACK", "hire 31", "refresh"}));
    free.busyHomes = {30, 31};
    OBL_CHECK_EQ(Do(Action::HireNewStaff, free), (Log{"homes CLEANERSHACK", "message STAFFB_NOSPACE_CLEANERS"}));
    Env unrecognized;
    unrecognized.hireType = 9;
    OBL_CHECK_EQ(Do(Action::HireNewStaff, unrecognized), Log{});
    // the other three home kinds and their messages
    Env reps;
    reps.hireType = 3;
    OBL_CHECK_EQ(Do(Action::HireNewStaff, reps), (Log{"homes REP", "message STAFFB_NOHOME_REPS"}));
    Env guards;
    guards.hireType = 4;
    OBL_CHECK_EQ(Do(Action::HireNewStaff, guards), (Log{"homes BAYWATCH", "message STAFFB_NOHOME_LIFEGUARDS"}));
    Env builders;
    builders.hireType = 1;
    OBL_CHECK_EQ(Do(Action::HireNewStaff, builders), (Log{"homes BUILDERYARD", "message STAFFB_NOHOME_BUILDERS"}));
}

OBL_TEST_CASE("gui_actions.dismiss_confirmation_and_firing")
{
    OBL_CHECK_EQ(Do(Action::DismissStaff), Log{"show ConfirmDismiss for 40 modal"});
    OBL_CHECK_EQ(Do(Action::StaffFire), Log{"show ConfirmDismiss for 7 modal"});
    OBL_CHECK_EQ(Do(Action::DismissConfirm, Env{}, 4), (Log{"dismiss 77 7", "closeid 4", "close for 7", "refresh"}));
}

OBL_TEST_CASE("gui_actions.staff_navigation_and_hours")
{
    OBL_CHECK_EQ(Do(Action::NextStaff), Log{"select 8"});
    OBL_CHECK_EQ(Do(Action::PrevStaff), Log{}); // the neighbour is the provider itself
    OBL_CHECK_EQ(Do(Action::StaffHome), Log{"select 77"});
    OBL_CHECK_EQ(Do(Action::StaffSend), (Log{"closeall", "context 9 7", "mode 9"}));
    OBL_CHECK_EQ(Do(Action::IncStaffHours), Log{"inc shift 7"});
    OBL_CHECK_EQ(Do(Action::DecStaffHours), Log{"dec shift 7"});
}

OBL_TEST_CASE("gui_actions.wages_and_shifts_are_copied_to_the_class")
{
    OBL_CHECK_EQ(Do(Action::SetStaffWages), (Log{"class 9", "wage 21 55", "happiness 21", "wage 22 55", "happiness 22"}));
    OBL_CHECK_EQ(Do(Action::StaffSetAllWages), (Log{"class 9", "wage 21 55", "happiness 21", "wage 22 55", "happiness 22"}));
    OBL_CHECK_EQ(Do(Action::SetStaffShift), (Log{"class 9", "shift 21 8-16", "shift 22 8-16"}));
    OBL_CHECK_EQ(Do(Action::StaffSetAllShift), (Log{"class 9", "shift 21 8-16", "shift 22 8-16"}));
}

OBL_TEST_CASE("gui_actions.staff_icons_select_by_class_and_toggle")
{
    OBL_CHECK_EQ(Do(Action::CleanerIcon), (Log{"select staff 11 0", "selection=99"}));
    OBL_CHECK_EQ(Do(Action::RepIcon), (Log{"select staff 9 1", "selection=99"}));
    OBL_CHECK_EQ(Do(Action::LifeguardIcon), (Log{"select staff 7 2", "selection=99"}));
    OBL_CHECK_EQ(Do(Action::MechanicIcon), (Log{"select staff 8 3", "selection=99"}));
    OBL_CHECK_EQ(Do(Action::SecurityIcon), (Log{"select staff 12 4", "selection=99"}));
    OBL_CHECK_EQ(Do(Action::BuilderIcon), (Log{"select staff 10 5", "selection=99"}));
    Env env;
    env.selection = 99;
    OBL_CHECK_EQ(Do(Action::CleanerIcon, env), (Log{"select staff 11 0", "selection=-1"}));
    env.hasStaff = false;
    OBL_CHECK_EQ(Do(Action::CleanerIcon, env), Log{});
}

OBL_TEST_CASE("gui_actions.provider_requirements")
{
    OBL_CHECK(NeedsProvider(Action::NextBuilding));
    OBL_CHECK(NeedsProvider(Action::SetStaffShift));
    OBL_CHECK(!NeedsProvider(Action::DismissStaff));
    OBL_CHECK(NeedsBrowsedStaff(Action::DismissStaff));
    OBL_CHECK(NeedsHome(Action::StaffHome));
    OBL_CHECK(!NeedsHome(Action::StaffSend));
}

OBL_TEST_CASE("gui_actions.menu_actions_open_their_screens")
{
    OBL_CHECK_EQ(Do(Action::None), Log{});
    OBL_CHECK_EQ(Do(Action::ShowMainMenu), (Log{"closeall", "show MainMenu"}));
    OBL_CHECK_EQ(Do(Action::ExtrasMenu), (Log{"closeall", "show ExtrasScreen", "destroy credits"}));
    OBL_CHECK_EQ(Do(Action::CreditsMenu), (Log{"closeall", "show CreditsScreen", "create credits"}));
    OBL_CHECK_EQ(Do(Action::ShowScenSel), (Log{"sandbox 0", "closeall", "show ScenarioScreen"}));
    OBL_CHECK_EQ(Do(Action::PlaySandBox), (Log{"sandbox 1", "closeall", "show ScenarioScreen"}));
}

OBL_TEST_CASE("gui_actions.start_new_game_picks_the_loading_screen_by_parity")
{
    Env even;
    even.rand = 12;
    OBL_CHECK_EQ(Do(Action::StartNewGame, even), (Log{"rand", "show LoadingScreen2", "state 5"}));
    Env odd;
    odd.rand = 7;
    OBL_CHECK_EQ(Do(Action::StartNewGame, odd), (Log{"rand", "show LoadingScreen1", "state 5"}));
}

OBL_TEST_CASE("gui_actions.exit_and_reset_ask_then_change_the_game_state")
{
    OBL_CHECK_EQ(Do(Action::ExitGame), Log{"show ExitConfirmBox modal"});
    OBL_CHECK_EQ(Do(Action::ExitGameConfirm), Log{"state 1"});
    OBL_CHECK_EQ(Do(Action::ResetGame), Log{"show ResetConfirmBox modal"});
    OBL_CHECK_EQ(Do(Action::ResetGameConfirm), Log{"state 4"});
    OBL_CHECK_EQ(Do(Action::WonGame), Log{"state 6"});
    OBL_CHECK_EQ(Do(Action::ExitWindows), Log{"show ExitConfirmBox for 7 modal"});
    OBL_CHECK_EQ(Do(Action::ExitWindowsConfirm), Log{"quit"});
}

OBL_TEST_CASE("gui_actions.reset_progress_confirms_then_rewrites_the_progress")
{
    OBL_CHECK_EQ(Do(Action::ResetProgress), Log{"show ResetProgressConfirm modal"});
    OBL_CHECK_EQ(Do(Action::ResetProgressConfirm, {}, 12), (Log{"closeid 12", "reset progress", "save progress"}));
}
