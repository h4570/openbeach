#pragma once

#include "core/char16.h"
#include "core/ascii_case.h"
#include "core/float_to_int.h"
#include "game/ui/gui_canvas.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace obl::gui_modes_tools {

enum class CameraFrameSet {
    None,
    A, // 800..1023: ui3_cam_a_*
    B, // 1024..1279: ui3_cam_b_*
    C, // >= 1280: ui3_cam_c_*
};

CameraFrameSet SelectCameraFrameSet(int width);

void DrawCameraFrame(gui_canvas::Canvas& canvas, int width, int height, float time);

// Thumbnails of the photo strip: x of the first one and the step, y is `height - kCameraThumbRaise`.
inline constexpr int kCameraThumbX = 0x32;
inline constexpr int kCameraThumbStep = 0x45;
inline constexpr int kCameraThumbRaise = 0x3b;

inline constexpr int kAlbumSideMargin = 0x5e;
inline constexpr int kAlbumPhotoOffset = 0x14;
inline constexpr int kAlbumComments = 0x23;
inline constexpr int kAlbumTextRenderer = 4;

inline int AlbumSurfaceWidth(int screenWidth)
{
    return screenWidth - kAlbumSideMargin;
}

int AlbumPhotoLimit(int screenWidth);

int AlbumPhotoLimitFor(int screenWidth);
// Thumbnails dropped from the front of a list of `count` before the new one is appended, so that it ends at `limit`.
int AlbumDropCount(int count, int limit);

// The text-table key of a comment: `CAMERA_COMMENT_<roll % kAlbumComments>` from the game's rand().
std::string AlbumCommentKey(int roll);

void DrawAlbumOverlay(gui_canvas::Canvas& canvas, int width, int height, const std::function<const Char16*()>& comment);

// The map pixel under one cursor coordinate in GuiMode::UpdatePaint: the zoom scale *multiplies* the cursor (in double, truncated to
// int; GuiModePlaceGrass::OnMouse divides) before the camera corner is added. The product of a float and a screen coordinate fits a
// double exactly.
inline int PaintMapPixel(int cursor, float zoomScale, int cameraCorner)
{
    return FloatToIntWrap(static_cast<double>(zoomScale) * static_cast<double>(cursor)) + cameraCorner;
}

// What Esc does in the default mode (GuiModeDefault::Update) once the followed object is dropped: with no dialog open it opens
// `InGameOptions`, otherwise it asks the first dialog of the list to close (GuiManager::CloseFirstDialog, which itself ignores a dialog
// that is not open yet or is protected).
inline bool EscapeOpensOptions(int openDialogs)
{
    return openDialogs == 0;
}

// The OnMouse of the four classes (Camera, Dozer, Default, SendStaff) turns the cursor it is handed (screen
// pixels) into map pixels the way GuiModePlaceGrass::OnMouse does - divided by the zoom scale in double, truncated to int - and adds
// the camera corner.
inline int MouseMapPixel(int cursor, float zoomScale, int cameraCorner)
{
    return FloatToIntWrap(static_cast<double>(cursor) / static_cast<double>(zoomScale)) + cameraCorner;
}

// Cursor shapes `CursorSetShape` takes in these modes (0 the plain arrow).
inline constexpr int kCursorArrow = 0;
inline constexpr int kCursorOverObject = 1; // default mode: an object is under the cursor
inline constexpr int kCursorDozer = 2;
inline constexpr int kCursorStaffOk = 4;
inline constexpr int kCursorStaffRefused = 5;
inline constexpr int kCursorStaffTarget = 6;

// Dozer: the bulldozer over the map, the arrow over a panel; a press removes only on the map and only where something can go.
inline int DozerCursorShape(bool overGui)
{
    return overGui ? kCursorArrow : kCursorDozer;
}
inline bool DozerRemoves(bool overMap, bool canRemove)
{
    return overMap && canRemove;
}
inline const char* DozerClickSound(bool removes)
{
    return removes ? "Cash" : "click_bad_01";
}

// SendStaff: arrow over a panel; over the map the shape follows the target flag first, then whether the tile can take the order.
inline int SendStaffCursorShape(bool overGui, bool targetsObject, bool canDirect)
{
    if (overGui) return kCursorArrow;
    return targetsObject ? kCursorStaffTarget : canDirect ? kCursorStaffOk : kCursorStaffRefused;
}
// A press sends the order only over the map on a tile that can take it; the mode then returns to the default one (as does Esc).
inline bool SendStaffSends(int left, bool overMap, bool canDirect)
{
    return left == 1 && overMap && canDirect;
}

// Default: the arrow over a panel, over a blocking dialog or over nothing, the pointing hand over an object.
inline int DefaultCursorShape(bool overGui, bool blockingDialog, bool objectUnder)
{
    return overGui || blockingDialog || !objectUnder ? kCursorArrow : kCursorOverObject;
}

// DrawWorld of Dozer and SendStaff: the tile under the cursor gets a highlight, green (0, 255, 0) when the mode may act
// there (CanRemove / CanDirect answered yes), red (255, 0, 0) otherwise, packed into the renderer's pixel format as (r, g, 0). Drawn only while
// the cursor is over the map (the flag OnMouse keeps).
struct TileHighlightRgb {
    int r, g, b;
};
inline TileHighlightRgb TileHighlightColor(bool allowed)
{
    return allowed ? TileHighlightRgb{0, 255, 0} : TileHighlightRgb{255, 0, 0};
}

inline int SelectionScreenPoint(int pixel, float zoomScale)
{
    return FloatToIntWrap(static_cast<double>(pixel) * static_cast<double>(zoomScale));
}

struct SelectionFrame {
    bool shown;
    int x1, y1, x2, y2; // press corner and cursor; the four lines run (x1,y1)-(x2,y1)-(x2,y2)-(x1,y2)-(x1,y1)
};

inline SelectionFrame ComputeSelectionFrame(int cornerX, int cornerY, int cursorX, int cursorY, int screenWidth, int screenHeight)
{
    const int dx = cursorX - cornerX, dy = cursorY - cornerY;
    const bool far = (dx < 0 ? -dx : dx) > 5 || (dy < 0 ? -dy : dy) > 5;
    const bool inside = cornerX > 0 && cornerX < screenWidth && cornerY > 0 && cornerY < screenHeight;
    return {far && inside, cornerX, cornerY, cornerX + dx, cornerY + dy};
}

inline constexpr uint32_t kTileSurfaceMask = 0x7000;
inline constexpr uint32_t kTileBuildingBit = 0x10;
inline constexpr uint32_t kSceneryStateBeingRemoved = 4;
inline constexpr float kDozerFlagIncome =
        5.0f; // income and cash credited for a cleared tile; scenery pays three quarters of its build cost

inline bool IsBarePathTile(uint32_t flags)
{
    return (flags & 1) != 0 && (flags & 0x80000000u) != 0 && (flags & 0x10000000u) != 0;
}
inline uint32_t ClearBarePath(uint32_t flags)
{
    return flags & 0xefffff7fu;
}
inline uint32_t ClearSurface(uint32_t flags)
{
    return flags & 0xffff8fffu;
}
inline double SceneryRemovalIncome(uint32_t buildCost)
{
    return static_cast<double>(buildCost) * 0.75;
}

// `neighboursFree()` answers whether none of the eight neighbour cells has the building bit; it is asked only when the flags make it matter.
template<class NeighboursFree> bool DozerCanRemove(bool anyObject, bool removableScenery, uint32_t flags, NeighboursFree&& neighboursFree)
{
    if (!anyObject) {
        if ((flags & kTileSurfaceMask) != 0) return true;
        if (IsBarePathTile(flags) && neighboursFree()) return true;
    }
    return removableScenery;
}

// ---- the send-staff target rules (GuiModeSendStaff::CanDirect) ------------------------------------------------------------------------
// The rule follows the name of the member being sent (`m_Name` of the mode's `m_target`; exact byte comparison).
enum class StaffRule {
    Builder, // a building with a build or demolish job under the cursor
    Cleaner, // the toilet block under the cursor, or a scenery piece on the tile that needs cleaning
    SecurityGuard, // a guest on the tile
    Guests, // LIFEG_F, LIFEG_M, REP_M, REP_F: a guest on the tile
    Mechanic, // a building without jobs under the cursor, or scenery on the tile that needs repair or water
    None, // every other member: nothing to target, the answer is the tile test alone
};

inline StaffRule ClassifyStaffTarget(const char* name)
{
    if (ascii::EqualsNoCase(name, "BUILDER")) return StaffRule::Builder;
    if (ascii::EqualsNoCase(name, "CLEANER")) return StaffRule::Cleaner;
    if (ascii::EqualsNoCase(name, "SECURITYGUARD")) return StaffRule::SecurityGuard;
    for (const char* guests : {"LIFEG_F", "LIFEG_M", "REP_M", "REP_F"})
        if (ascii::EqualsNoCase(name, guests)) return StaffRule::Guests;
    if (ascii::EqualsNoCase(name, "MECHANIC")) return StaffRule::Mechanic;
    return StaffRule::None;
}

inline bool StaffTileAllowed(uint32_t flags)
{
    return (flags & kTileBuildingBit) == 0 && (flags & 0x80000000u) != 0;
}

inline bool IsStaffType(int objectType)
{
    return objectType > 6 && objectType < 13;
}
inline bool SelectionLevelWanted(bool preferSelected, int level)
{
    return preferSelected ? (level == 1 || level == 2) : level == 0;
}

inline void OrderSelectionAxis(float press, float release, float& low, float& high)
{
    const bool pressFirst = !(press >= release);
    low = pressFirst ? press : release;
    high = pressFirst ? release : press;
}

} // namespace obl::gui_modes_tools

namespace obl::ui {
struct GuiMode;
struct GuiManager;
} // namespace obl::ui

namespace obl::gui {
bool RegisterToolModeDrivers();

// GuiModeCamera::ComposePhoto calls finished since the start, on every path (the `gui-photos-composed` probe: a click on the camera button is
// dropped while the previous photo is still pending, so a test waits for this, not for the strip, which stops growing at its limit).
int AlbumComposeCount();
// The scenery dozer on tile x/y (GuiModeDozer::CanRemove and its removal, without the mode object), shared by the dozer's click preview and the
// `dozer` command (command_executor.cpp): RemoveAt removes the scenery, the bare path or the surface there and credits the removal income.
bool DozerCanRemoveAt(uint32_t x, uint32_t y);
bool DozerRemoveAt(uint32_t x, uint32_t y);

namespace gui_modes {
// A new tool mode `mode` (0 paint, 2 camera, 4 default, 6 none, 9 send staff, 10 dozer), as GuiManager::SwitchMode builds it; null for
// any other mode.
ui::GuiMode* ConstructTool(int mode, ui::GuiManager* manager);
} // namespace gui_modes
} // namespace obl::gui
