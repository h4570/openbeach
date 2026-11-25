#pragma once

#include "formats/mod_pack.h"

#include <cstdint>
#include <iterator>

// Where the player's input goes when mods' presentation scripts run (docs/architecture/input.md): which default behaviors a mod has
// taken over, and the hook through which the platform hands finger events to the scripts before its own touch recognizer. Plain
// state, set by the game's side of the scripts (presentation_scripts.cpp) and read by the platform and the camera; without mods
// nothing is taken over and no hook is set, so the defaults run as they always did.
namespace obl::mods {

// A default behavior a mod may take over, in the order of mod_pack::kTakeOverAreas.
enum class Area {
    InputTouch, // the touch recognizer (platform/touch_gestures.h): a tap is a click, a drag moves the camera
    InputMouseDrag, // the camera drag with the right mouse button held
    CameraEdgeScroll, // the camera scrolling while the cursor touches the screen edge
};
inline constexpr int kAreaCount = 3;
static_assert(std::size(mod_pack::kTakeOverAreas) == kAreaCount);

// Whether a running mod has taken over the area: the engine skips that default while it holds.
bool IsTakenOver(Area area);
void SetTakenOver(Area area, bool taken);

enum class TouchPhase { Down, Move, Up, Cancel };

// One finger event in game pixels (-300, -300 off the game's image).
struct TouchEvent {
    int64_t finger = 0;
    TouchPhase phase = TouchPhase::Down;
    int x = 0;
    int y = 0;
};

// The scripts' side of a finger event: true when a mod consumed it. Set while scripts run, null otherwise.
using TouchHandler = bool (*)(const TouchEvent& event);
void SetTouchHandler(TouchHandler handler);
// Hands the event to the handler; false (not consumed) without one.
bool RouteTouch(const TouchEvent& event);

} // namespace obl::mods
