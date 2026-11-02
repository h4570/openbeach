#pragma once

#include "font.h"
#include "gui_types.h"

#include <cstdint>

namespace obl::ui {
struct Renderer; // game/ui/renderer_types.h
} // namespace obl::ui
namespace obl::ui {

// Entry of the open-dialogs list (GuiManager::ShowDialog), the value of a std::list node.
struct GuiMode; // gui_modes_types.h

struct GuiManagerDialogEntry {
    GuiDialogData* m_dialog;
    int32_t m_surface; // Renderer surface the dialog is composed on (released when it closes)
    int32_t m_state; // 0 opening, 1 active, 2 pending close; removal belongs to UpdateDialogs
    void* m_provider; // object whose variables the dialog shows (becomes GuiManager::m_variableProvider while it updates)
};

struct GuiManagerDialogNode {
    GuiManagerDialogNode* m_next;
    GuiManagerDialogNode* m_previous;
    GuiManagerDialogEntry* m_entry;
};
// Singly linked DialogItem windows the GUI script created; LoadScript takes the list from the parser.
struct GuiManagerDefinitionNode {
    GuiManagerDefinitionNode* m_next;
    GuiDialogData* m_dialog;
};
// Singly linked list of every widget the GUI script created, taken from the parser the same way.
struct GuiManagerWidgetNode {
    GuiManagerWidgetNode* m_next;
    GuiWidget* m_widget;
};

// The message history list behind the "TickerList800/640" dialogs (GuiManager::m_tickerList).
struct TickerList {
    void* m_listBox; // GuiListBox of the open dialog, null until SetVisible(1) found it
    uint8_t m_visible;
    uint32_t m_rows; // rows added by the latest Rebuild

    void Update();
    void SetVisible(uint8_t visible);
};

struct GuiCreditsLine {
    GuiWidget* m_text;
    int32_t m_kind;
};
struct GuiCreditsNode {
    GuiCreditsNode* m_next;
    GuiCreditsNode* m_previous;
    GuiCreditsLine* m_line; // deleted by the functor (its first field is an object with a deleting destructor)
};
// k_CreditEntries (205 records): the i-th line of the roll.
struct GuiCreditEntry {
    int32_t m_font; // GuiText font index
    int32_t m_number; // %d of CREDIT_ENTRY_%d
    uint32_t m_color;
    uint8_t m_long; // 1 = the next line follows after 30 ticks instead of 15
    int32_t m_kind; // copied to GuiCreditsLine::m_kind
};
struct GuiCredits {
    uint32_t m_nextEntry; // index into k_CreditEntries; zeroed by the constructor and by Reset
    GuiCreditsNode* m_lines;
    int32_t m_delay; // ticks until the next line is added; zeroed only by Reset
    float m_endTimer; // seconds left once the last line has scrolled out
    uint8_t m_ending; // the last line is gone, m_endTimer counts down

    // The credits roll (gui_manager_frame.cpp over gui_manager_frame.h).
    GuiCredits* Construct();
    void Destruct();
    void Reset();
    void Update(float dt);
    bool IsFinished();
};

// The manager: fonts, script trees, open dialogs, mode, HUD surfaces, message balloon, radar panel, progress bar, tooltip,
// music player, ticker list and credits.
struct GuiManager {
    void ShowTooltip(GuiWidget* widget, int x, int y, const char* text);
    void HideTooltip(GuiWidget* widget);
    float GetTooltipDelay();

    ui::Renderer* m_renderer;
    const void* m_fonts[10]; // BitmapFont per font index (GuiList reads the line size, BitmapFont::m_size)
    TextRenderer* m_textRenderers[10];
    // the values SetContext(i, v) writes; LoadScript resets [0] 0, [1] 1, [2] 0x11, [7] 0, [10] 2, [11] 0,
    // [12] -1. [0] building picked for placing (PlaceBuilding), [5] scenery picked in the palette (both indices into
    // WorldParams), [10] staff type the browser lists, [11] the type HireNewStaff hires (0 cleaners, 1 builders,
    // 2 mechanics, 3 reps, 4 lifeguards, 5 security), [12] the staff member the browser shows.
    int32_t m_context[13];
    GuiManagerWidgetNode* m_widgets; // every widget the script created (ToggleSceneryPanel re-skins the type 0 buttons)
    GuiManagerDefinitionNode* m_definitions; // DialogItem windows; ShowDialog looks names up here
    GuiWidget* m_mainItem; // the HUD bar (MainItem); ignored while m_mode is 2 (camera)
    // object whose window is open (the dialogs' GUI variables read it), set to the provider of each active dialog
    // the mouse is offered to; its GetVariableText(key, out) answers unknown keys
    void* m_variableProvider;
    // selected / followed object id, -1 none (ToggleSelection, UpdateFollowCamera); the world's camera
    // keyboard input, MoveCamera, a radar map click and Esc in the default mode clear it
    int32_t m_selection;
    uint8_t m_resolutionRequested; // RequestResolution: a different size is pending
    int32_t m_requestedWidth;
    int32_t m_requestedHeight;
    int32_t m_barHeight; // height of the bottom bar surface (SyncBarSurfaces: 0x4b below 800 px wide, else 0x5e)
    int32_t m_barSurface; // Renderer surface of the HUD bar, blitted at (0, screen height - m_barHeight)
    int32_t m_radarSurface; // the radar panel (ToggleRadarSize clears it)
    int32_t m_cursorSurface; // the software cursor is drawn on it, 32x32
    int32_t m_progressSurface; // the progress bar
    GuiManagerDialogNode* m_dialogs; // sentinel of the open dialogs, drawn last to first
    uint8_t m_softwareCursor; // SetSoftwareCursorEnabled
    const char* m_scriptTextKey; // text-table key of the GUI variable ScriptText (Construct clears it)
    const char* m_messageTextKey; // text-table key of MessageText, set by ShowMessageBox before it opens the dialog
    int32_t m_messageMode; // 1 short / 2 long balloon (ShowMessage, called by the feedback manager)
    GuiWidget* m_balloon; // message balloon, replaced by the next message, deleted by LoadScript
    uint8_t m_balloonActive; // set by ShowMessage; CanShowMessage answers whether this is 0
    // the strip the balloon scrolls in (TickerText bounds, SetTickerBounds): it enters at the right and
    // stops at the left; the overlay clips the balloon to it
    int32_t m_tickerLeft, m_tickerTop, m_tickerRight, m_tickerBottom;
    // rectangle of the radar panel sprite (CreateRadar x/y + its size): where it is drawn and the mouse hover box
    int32_t m_panelX0, m_panelY0, m_panelX1, m_panelY1;
    uint8_t m_radarEnabled; // the HUD has the radar (camera mode clears it, its Esc handler sets it)
    uint8_t m_radarBig; // 1: panel drawn big (the map widget is shown); picks the graphic of the next toggle
    uint8_t m_radarDirty; // the panel sprite has to be redrawn
    GuiWidget* m_radarWidget; // GuiImage of the map picture
    GuiWidget* m_radarButton; // ToggleRadarSize button
    SpriteInstance* m_radarSprite; // panel sprite (scanner_paneldown / panelup)
    int32_t m_viewX0, m_viewY0; // camera rectangle on the radar: the widget's top-left corner
    int32_t m_viewX1, m_viewY1; // and its far corner, from the renderer and map sizes (UpdateRadarViewRect)
    uint16_t m_colorViewRect; // packed 16-bit colours: green camera rectangle,
    uint16_t m_colorMarkerBlack; // black pixel marker,
    uint16_t m_colorMarkerRed; // red fill,
    uint16_t m_colorMarkerTeal; // teal fill,
    uint16_t m_colorMarkerGrey; // grey pixel marker (DrawRadarMarkers)
    uint8_t m_radarHovered; // the cursor is over the panel (IsRadarHovered)
    // only a map click moves the camera while it is set (Construct and the default mode's Exit set it)
    uint8_t m_radarMouseArmed;
    GuiMode* m_modeObject; // the current mode, owned here
    // mode of m_modeObject (0 paint .. 10 dozer, 3 has no object); 6 draws only the cursor and the tooltip, 2
    // (camera) no HUD bar or balloon and blocks the panel toggles; position memory is off while this or the request is 6
    int32_t m_mode;
    int32_t m_requestedMode; // SetMode writes it, SwitchMode (the next Update) applies it
    void* m_tooltip; // GuiTooltip
    GuiWidget* m_tooltipOwner; // only this widget may hide the current tooltip
    float m_tooltipCooldown; // positive: short delay after hiding; counted down by Update, clamped at 0
    void* m_musicPlayer; // audio::MusicPlayer (its Update runs every frame, the buttons call Command)
    void* m_tickerList; // TickerList (its Update runs every frame, the dialog calls SetVisible)
    // set by the DoCameraGrab action: the next overlay copies the screen interior into a surface
    uint8_t m_cameraGrabRequested;
    int32_t m_cameraGrabSurface; // the photo surface for the album, -1 none; GuiModeCamera::ComposePhoto resets it
    // progress bar panel: x 9, y = screen height - bar height, far corner from the sprite size (hover box)
    int32_t m_progressX0, m_progressY0, m_progressX1, m_progressY1;
    uint8_t m_progressVisible; // nothing is drawn while 0 (a clear call resets it)
    uint8_t m_progressBig; // 1: panel drawn up (the dialog is shown), 0: the small strip 0x41 lower
    uint8_t m_progressDirty; // the surface must be cleared and the panel sprite redrawn
    GuiWidget* m_progressButton; // ToggleProgressSize button, gets the mouse while the bar is visible
    SpriteInstance* m_progressSprite; // panel sprite (Destruct releases it even when null)
    GuiWidget* m_progressDialog; // goal dialog placed on the panel, null when none; drawn only while big
    // the cursor is over the bar; IsCursorOverGui returns it when no dialog claims the cursor
    uint8_t m_progressHovered;
    char m_progressDialogName[0x23]; // dialog name for ActivateProgressDialog (SetProgressDialog copies it unbounded)
    GuiCredits* m_credits; // while the credits run

    // The action-button walks (gui_manager_buttons.cpp over gui_manager_buttons.h): each visits every button the script
    // created and applies the change to those bound to `action`. SelectStaff cycles the staff of a class and returns the
    // selected object's id (`index` is the icon's rotating selection index).
    void DisableActionButtons(const char* action);
    void ResetActionButtons(const char* action);
    void FlashActionButtons(const char* action, bool flash);
    void BlinkActionButtons(const char* action, bool blink);
    void SetActionButtonGraphic(const char* action, const char* sprite);
    void UpdateStaffButtons(uint8_t staffType, bool hasStaff);
    void UpdateFerryButtonGraphic();
    static uint32_t SelectStaff(int objectClass, uint32_t* index);

    // The frame: queries, modes, the mouse dispatch, the follow camera, the tile outline, the credits object and the
    // bar surfaces (gui_manager_frame.cpp over gui_manager_frame.h).
    void SetFont(int index, const void* font);
    void SetTextRenderer(int index, void* renderer);
    void RequestResolution(int width, int height);
    void SetSoftwareCursorEnabled(bool enabled);
    void CreateCredits();
    void DestroyCredits();
    void OpenVideoOptions();
    void DrawSurfaces();
    void LoadScript(const char* path);
    void Update(float dt);
    GuiManager* Construct();
    void Destruct();
    void SetMode(int mode);
    void SwitchMode();
    bool IsCursorOverGui(bool includeRadar);
    bool IsMainItemHovered();
    bool IsRadarHovered();
    void HideProgressBar();
    void UpdateFollowCamera();
    void DrawTileOutline(uint32_t x, uint32_t y, uint32_t color);
    void UpdateMouse();
    void UpdateProgressBarMouse(int x, int y, int state);
    void SyncBarSurfaces();
    bool CreateBarSurfaces();
    bool CanUseWorldCameraInput(); // the world view asks before it scrolls or zooms with the mouse

    // The radar and the progress panel below the HUD (gui_radar.cpp over the rectangle math of gui_radar.h).
    void CreateRadar(int x, int y, const char* image);
    void SetProgressDialog(const char* name);
    void UpdateRadarViewRect();
    void UpdateRadarMouse(int x, int y, int state);
    void ClickMoveRadarCamera(int x, int y, int state);
    void ShiftRadarViewRect();
    void DrawRadar();
    void CreateProgressBar();
    void RelayoutProgressBar();
    void ActivateProgressDialog();
    void DrawProgressBar();

    // The dialog windows, panel toggles, message balloon and ticker (gui_manager_dialogs.cpp over gui_manager_dialogs.h and
    // gui_manager_messages.h).
    void RememberDialogPosition(const int32_t* rect, int32_t key);
    bool IsDialogOpen(const char* name);
    int GetDialogMinimized(const char* name);
    void* VariableProvider();
    void RefreshAllDialogs();
    void InvalidateAllDialogs(uint8_t clear);
    void InvalidateDialog(const char* name, uint8_t clear);
    bool CloseDialog(const char* name);
    void SetDialogMinimized(const char* name, int value);
    void SetContext(int index, int value);
    void AnimateDialog(const char* name, int type, float speed, int delay);
    bool ToggleDialog(const char* name, void* provider, bool modal);
    void ShowDialogWithText(const char* name, const char* textKey, void* provider, bool modal);
    bool CloseDialogById(int id);
    void CloseAllDialogs();
    void CloseFirstDialog();
    void SelectDialogTab(const char* name, int tab);
    bool IsBlockingDialogOpen();
    int GetDialogCount();
    void ToggleLoadGameMenu();
    void ToggleSaveGameMenu();
    void ToggleInGameOptions();
    void ToggleMusicPanel();
    void ToggleBuildPanel();
    void ToggleSceneryPanel();
    void ShowPauseUnlessScenarioEnded();
    void ClosePauseDialog();
    void UpdateDialogs(float dt);
    void* FindListBox(const char* name);
    void ShowMessageBox(const char* name, const char* messageKey, void* provider, bool modal);
    void ShowMessage(const Char16* text, int mode);
    void ShowMessageKey(const char* key, int mode);
    bool ShowDialog(const char* name, void* provider, bool modal);
    bool CloseDialogByProvider(void* provider);
    bool CanShowMessage();
    void UpdateTickerAnimation(float dt);
    void SetTickerBounds(int left, int top, int right, int bottom);
};

} // namespace obl::ui
