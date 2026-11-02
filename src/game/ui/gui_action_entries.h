#pragma once
namespace obl::gui {
bool RegisterGuiActionDrivers();
void RefreshCommandDialogs();

// The GUI script actions: X(Action, entry). Buttons store the entry (kActions in gui_controls.cpp).
// clang-format off
#define OBL_GUI_ACTIONS(X) \
    X(IncDist, IncMaxDist) X(DecDist, DecMaxDist) X(CameraMode, CameraMode) \
    X(DoCameraGrab, DoCameraGrab) X(LockCamera, LockCamera) X(ZoomIn, ZoomIn) \
    X(ZoomOut, ZoomOut) X(ToggleRadarSize, ToggleRadarSize) \
    X(ToggleProgressSize, ToggleProgressSize) X(ShowTickerList800, ShowTickerList800) \
    X(ShowTickerList640, ShowTickerList640) X(MusicList, MP3List) \
    X(MusicRefresh, MP3Refresh) X(MusicRefreshConfirm, MP3RefreshConfirm) \
    X(MusicVolUp, MP3VolUp) X(MusicVolDown, MP3VolDown) X(MusicSun, MP3Sun) \
    X(MusicMoon, MP3Moon) X(MusicRepeat, MP3Repeat) X(MusicShuffle, MP3Shuffle) \
    X(MusicDelete, MP3Delete) X(MusicMoveUp, MP3MoveUp) X(MusicMoveDown, MP3MoveDown) \
    X(MusicPlay, MP3Play) X(MusicNext, MP3Next) X(MusicPrevious, MP3Previous) \
    X(MusicStop, MP3Stop) X(MusicPause, MP3Pause) X(MusicDefault, MP3Default) \
    X(ExitDialog, ExitDialog) X(ExitDialogResume, ExitDialogResume) \
    X(ExitDialogMessage, ExitDialogMessage) X(ClosePause, ClosePause) X(ShowGoal, ShowGoal) \
    X(InGameOptions, InGameOptions) X(ExitOptions, ExitOptions) \
    X(ResetVideoOptions, ResetVideoOptions) X(ResetAudioOptions, ResetAudioOptions) \
    X(LoadGameMenu, LoadGameMenu) X(LoadGamePanel, LoadGamePanel) X(SaveGamePanel, SaveGamePanel) \
    X(ExitLoadGame, ExitLoadGame) X(LoadGame, LoadGame) X(SaveGame, SaveGame) \
    X(SaveGameConfirm, SaveGameConfirm) X(DeleteSaveGame, DeleteSaveGame) \
    X(DeleteLoadGame, DeleteLoadGame) X(DeleteSaveGameConfirm, DeleteSaveGameConfirm) \
    X(DeleteLoadGameConfirm, DeleteLoadGameConfirm) X(ExitSaveOK, ExitSaveOK) \
    X(BuildList, BuildList) X(PlaceBuilding, PlaceBuilding) X(SaveGridMap, SaveGridMap) \
    X(PaintMap, PaintMap) X(ExitBuildMode, ExitBuildMode) X(SceneryPanel, SceneryPanel) \
    X(ExitSceneryPanel, ExitSceneryPanel) X(MinSceneryPanel, MinSceneryPanel) \
    X(SceneryDozer, SceneryDozer) \
    X(ObjectBrowser, ObjectBrowser) X(GuestBrowser, GuestBrowser) X(SellButton, SellButton) \
    X(ConfirmSell, ConfirmSell) X(BreakObject, BreakObject) X(CallRepair, CallRepair) \
    X(CallCleaner, CallCleaner) X(OpenBuilding, OpenBuilding) X(CloseBuilding, CloseBuilding) \
    X(NextBuilding, NextBuilding) X(PrevBuilding, PrevBuilding) X(PowerPanel, PowerPanel) \
    X(MoneyPanel, MoneyPanel) X(IslandStatus, IslandStatus) X(FerryPanel, FerryPanel) \
    X(CleanerBrowser, CleanerBrowser) X(BuilderBrowser, BuilderBrowser) X(MechanicBrowser, MechanicBrowser) \
    X(RepBrowser, RepBrowser) X(LifeguardBrowser, LifeguardBrowser) X(SecurityBrowser, SecurityBrowser) \
    X(HireNewStaff, HireNewStaff) X(DismissStaff, DismissStaff) X(DismissConfirm, DismissConfirm) \
    X(StaffFire, StaffFire) X(NextStaff, NextStaff) X(PrevStaff, PrevStaff) \
    X(StaffSend, StaffSend) X(StaffHome, StaffHome) X(IncStaffHours, IncStaffHours) \
    X(DecStaffHours, DecStaffHours) X(SetStaffWages, SetStaffWages) X(SetStaffShift, SetStaffShift) \
    X(StaffSetAllWages, StaffSetAllWages) X(StaffSetAllShift, StaffSetAllShift) \
    X(CleanerIcon, CleanerIcon) X(BuilderIcon, BuilderIcon) X(MechanicIcon, MechanicIcon) \
    X(RepIcon, RepIcon) X(LifeguardIcon, LifeguardIcon) X(SecurityIcon, SecurityIcon)     X(None, None) X(StartNewGame, StartNewGame) X(ExitWindows, ExitWindows)     X(ExitWindowsConfirm, ExitWindowsConfirm) X(ShowMainMenu, ShowMainMenu) X(ExtrasMenu, ExtrasMenu)     X(ShowScenSel, ShowScenSel) X(PlaySandBox, PlaySandBox) X(ResetGame, ResetGame)     X(ResetGameConfirm, ResetGameConfirm) X(ExitGame, ExitGame) X(ExitGameConfirm, ExitGameConfirm)     X(WonGame, WonGame) X(CreditsMenu, CreditsMenu) X(ResetProgress, ResetProgress)     X(ResetProgressConfirm, ResetProgressConfirm)
// clang-format on

namespace gui_action_entries {
#define OBL_DECLARE_ACTION(Id, Symbol) void __cdecl Symbol(int arg);
OBL_GUI_ACTIONS(OBL_DECLARE_ACTION)
#undef OBL_DECLARE_ACTION
} // namespace gui_action_entries
} // namespace obl::gui
