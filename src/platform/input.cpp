// The SDL backend of the Input object (input_backend.h, input_ops.cpp): SDL supplies keyboard, mouse and camera drag. Mouse
// buttons, wheel and drag motion become the object's buffered records (Input::MouseRecord) directly; no device is acquired.

#include "platform/input.h"

#include "game/ui/input_backend.h"

#include "game/game/game_accessors.h"
#include "game/ui/gui_manager_types.h"
#include "core/config.h"
#include "platform/cursor.h"
#include "platform/display_window.h"
#include "input_mapping.h"
#include "core/log.h"
#include "platform/sdl_present.h"
#include "platform/touch_gestures.h"

#include <windows.h>
#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <cstdlib>
#include <cstring>

namespace obl::platform {

namespace {

bool g_hidden;
bool g_inputFocused = true; // now: the event watch sets it when SDL queues a focus event
bool g_queueFocused = true; // at the drain position of ReadSdlInputEvents
POINT g_sdlMouseClient{-300, -300};
UnitAccumulator g_sdlWheel;
UnitAccumulator g_sdlRelativeX, g_sdlRelativeY;
AbsoluteDrag g_absoluteDrag; // kAbsoluteDrag: the pointer position the drag measures from
ui::SdlRelativeDragState g_sdlRelativeState;
KeyLatch g_keys;
// Fingers (touch_gestures.h): SDL's touch-to-mouse events are off then, and the gestures fill the same records the mouse does.
TouchGestures g_touch;
// The cursor is where a finger put it, not where a mouse is. It goes away once the finger is up and the game has read it, so
// that the last tap does not leave a cursor at the screen edge, which scrolls the map.
bool g_touchCursor;
UnitAccumulator g_touchDragX, g_touchDragY;
double g_touchPendingX, g_touchPendingY; // finger motion the game's camera drag has not started to take yet (client pixels)

// The page locks the pointer for a right drag (web/site.js); SDL then reports motion as deltas.
bool PagePointerLocked()
{
#ifdef __EMSCRIPTEN__
    EmscriptenPointerlockChangeEvent status{};
    return emscripten_get_pointerlock_status(&status) == EMSCRIPTEN_RESULT_SUCCESS && status.isActive;
#else
    return false;
#endif
}

void ResetSdlKeyboard()
{
    g_keys.Reset();
    // Window messages may already have queued keys before a focus-loss/reset message.
    // Discard them here so they cannot resurrect a key when the game next updates.
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
}

void ReadSdlInputEvents(ui::Input* input);
void ReadSdlTouchEvents();
void EnsureEventWatch();

// ---------------------------------------------------------------------------------------------
// Mouse records filled from SDL input (main thread) and drained by ReadMouse (main thread as well, updated once per game tick).

using MouseRecord = ui::Input::MouseRecord;
std::deque<MouseRecord> g_events;
uint32_t g_sequence;
// The mouse device: only its presence matters (a non-null m_mouseDevice enables the mouse update).
int g_mouseDevice;

bool Dragging()
{
    return ui::CurrentRelativeDrag().on;
}

POINT DragAnchor()
{
    const input_state::Point anchor = ui::CurrentRelativeDrag().anchor;
    return {anchor.x, anchor.y};
}

void PushEvent(uint32_t offset, uint32_t data)
{
    if (!g_inputFocused) return;
    if (g_events.size() >= 256) g_events.pop_front();
    g_events.push_back({offset, data, static_cast<uint32_t>(SDL_GetTicks()), ++g_sequence, 0});
}

void RestoreSdlMousePosition()
{
    // The offscreen driver (hidden mode on SDL hosts) has no pointer: its state is (0, 0), which would scroll the edge.
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "offscreen") == 0) return;
    SDL_Window* focus = SDL_GetMouseFocus();
    if (!focus || SDL_GetWindowID(focus) != SdlGameWindowId()) return;
    float x, y;
    SDL_GetMouseState(&x, &y);
    g_sdlMouseClient = {static_cast<LONG>(std::floor(x)), static_cast<LONG>(std::floor(y))};
}

void ReadSdlInputEvents(ui::Input* input)
{
    EnsureEventWatch();
    SDL_Event event;
    // One drain preserves ordering between keyboard, mouse and focus: an event counts by the focus at its place in the queue
    // (g_queueFocused), which the event watch's current state (g_inputFocused) is already ahead of. Peep does not pump again.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_MOUSE_LEAVE, SDL_EVENT_MOUSE_WHEEL) > 0) {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            if (event.window.windowID == SdlGameWindowId()) {
                g_queueFocused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
                if (!g_queueFocused)
                    ui::ResetInput(input, false); // A later gain/press in the same queue must survive.
                else
                    RestoreSdlMousePosition(); // SDL suppresses motion at an unchanged position.
            }
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
            if (event.window.windowID == SdlGameWindowId()) g_sdlMouseClient = {-300, -300};
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            // Hidden posted keys have no physical keyboard focus and may carry windowID=0.
            if ((event.key.windowID != SdlGameWindowId() && !(g_hidden && event.key.windowID == 0)) || !g_queueFocused) continue;
            g_keys.OnKey(SdlScancodeToKeyCode(event.key.scancode), event.type == SDL_EVENT_KEY_DOWN, event.key.repeat);
            continue;
        }
        if (!g_queueFocused) continue;
        if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == SdlGameWindowId()) {
            if (Dragging()) {
                // In relative mode x/y is SDL's hidden, window-clamped position; xrel/yrel are device units. So it is under
                // the page's pointer lock; without either (kAbsoluteDrag) the drag follows the visible pointer. Keep
                // fractions so a scale cannot lose slow motion.
                AbsoluteDrag::Delta motion{event.motion.xrel, event.motion.yrel};
                if (kAbsoluteDrag && !PagePointerLocked()) motion = g_absoluteDrag.Move(event.motion.x, event.motion.y);
                int dx = g_sdlRelativeX.Add(motion.x), dy = g_sdlRelativeY.Add(motion.y);
                platform::InjectRelativeMotion(dx, dy);
                continue;
            }
            g_sdlMouseClient = {static_cast<LONG>(std::floor(event.motion.x)), static_cast<LONG>(std::floor(event.motion.y))};
            g_touchCursor = false;
        } else if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                event.button.windowID == SdlGameWindowId()) {
            if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) continue;
            g_sdlMouseClient = {static_cast<LONG>(std::floor(event.button.x)), static_cast<LONG>(std::floor(event.button.y))};
            g_touchCursor = false;
            PushEvent(event.button.button == SDL_BUTTON_LEFT ? input_state::kRecordLeft : input_state::kRecordRight,
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 0x80 : 0);
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID == SdlGameWindowId()) {
            // SDL expresses vertical wheel motion in notches; DirectInput uses signed units of 120.
            // Keep fractional units between events, including direction reversals on a smooth wheel.
            int whole = g_sdlWheel.Add(SdlWheelUnits(event.wheel.y, event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED));
            if (whole) PushEvent(input_state::kRecordWheel, static_cast<uint32_t>(whole));
        }
    }
    ReadSdlTouchEvents();
}

// Focus changes act when SDL queues them: the drag ends and queued keys and buttons are dropped at once (a focus loss must not
// leave a camera drag running until the next update), with the same order rules as the drain above. The drain still sees the
// event and applies it in queue order. Mouse motion refreshes the local cursor as soon as it arrives. SDL's Windows backend
// sends focus events only for the foreground window, so a hidden run's window keeps its focus; the tests push SDL focus events.
bool SDLCALL WatchSdlEvents(void*, SDL_Event* event)
{
    const uint32_t window = SdlGameWindowId();
    if ((event->type == SDL_EVENT_WINDOW_FOCUS_GAINED || event->type == SDL_EVENT_WINDOW_FOCUS_LOST) && window &&
            event->window.windowID == window) {
        g_inputFocused = event->type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        if (!g_inputFocused)
            ui::ResetInput(game::GetInput());
        else
            RestoreSdlMousePosition();
        RefreshLocalCursor();
    } else if (event->type == SDL_EVENT_MOUSE_MOTION && window && event->motion.windowID == window)
        RefreshLocalCursor();
    return true;
}

void EnsureEventWatch()
{
    static bool installed;
    if (installed || !SDL_WasInit(SDL_INIT_EVENTS)) return;
    installed = SDL_AddEventWatch(&WatchSdlEvents, nullptr);
    if (!installed) OBL_LOG("input: SDL_AddEventWatch failed: %s", SDL_GetError());
}

} // namespace

namespace {

// ---------------------------------------------------------------------------------------------
// Cursor mapping: the backend's ReadMouse maps the SDL client position (or a test's synthetic cursor) into
// the renderer's image rectangle, for any window style and size.

bool g_syntheticCursor;
bool g_syntheticClient;
POINT g_synthetic;

void* GameWindow()
{
    return game::GetGameWindow();
}

SIZE GameSize()
{
    const auto* renderer = game::GetRenderer();
    if (!renderer) return {800, 600};
    return {renderer->m_width, renderer->m_height};
}

POINT ClientToGame(void* window, POINT client)
{
    SIZE game = GameSize();
    int clientWidth = 0, clientHeight = 0;
    GameWindowClientSize(window, clientWidth, clientHeight);
    RECT image = ClientImageRect(window, game);
    // The screen edge scrolls the map through a bar too (display_geometry.h).
    const ui::DisplayPoint snapped = ui::SnapClientEdgeToImage(
            {static_cast<int>(client.x), static_cast<int>(client.y)}, {clientWidth, clientHeight}, FromWinRect(image));
    client = {snapped.x, snapped.y};
    // The mouse update accepts x == width / y == height, so the bars are rejected explicitly before its bounds check.
    if (client.x < image.left || client.x >= image.right || client.y < image.top || client.y >= image.bottom) return {-300, -300};
    return MapClientToGame(client, game, image);
}

// The camera drag motion is the finger's, in game pixels: the page shows the 800x600 game smaller on a phone, and the world should
// follow the finger rather than lag behind it.
double TouchDragScale()
{
    void* window = GameWindow();
    const SIZE game = GameSize();
    if (!window) return 1.0;
    const RECT image = ClientImageRect(window, game);
    const LONG width = image.right - image.left;
    return width > 0 ? static_cast<double>(game.cx) / static_cast<double>(width) : 1.0;
}

// GuiManager::m_mode of the modes that place things (the factory in gui_modes_place.cpp): where a press has to be chosen first.
constexpr int kGuiModePlaceScenery = 1, kGuiModePlaceBuilding = 5, kGuiModePlacePavement = 7, kGuiModePlaceGrass = 8;

TouchAim CurrentTouchAim()
{
    const ui::GuiManager* manager = game::GetGuiManager();
    if (!manager) return TouchAim::None;
    switch (manager->m_mode) {
    case kGuiModePlaceScenery:
    case kGuiModePlaceBuilding: return TouchAim::Confirm;
    case kGuiModePlacePavement:
    case kGuiModePlaceGrass: return TouchAim::Paint;
    default: return TouchAim::None;
    }
}

void PlaceTouchCursor(float x, float y)
{
    g_sdlMouseClient = {static_cast<LONG>(std::floor(x)), static_cast<LONG>(std::floor(y))};
    g_touchCursor = true;
}

void ApplyTouchActions()
{
    using Kind = TouchAction::Kind;
    for (const TouchAction& action : g_touch.Take()) {
        switch (action.kind) {
        case Kind::Move: PlaceTouchCursor(action.x, action.y); break;
        case Kind::LeftDown:
        case Kind::LeftUp:
            PlaceTouchCursor(action.x, action.y);
            PushEvent(input_state::kRecordLeft, action.kind == Kind::LeftDown ? 0x80 : 0);
            break;
        case Kind::RightDown:
            PlaceTouchCursor(action.x, action.y);
            g_touchDragX.Reset();
            g_touchDragY.Reset();
            g_touchPendingX = g_touchPendingY = 0;
            PushEvent(input_state::kRecordRight, 0x80);
            break;
        case Kind::RightUp: PushEvent(input_state::kRecordRight, 0); break;
        case Kind::Drag:
            g_touchPendingX += action.x;
            g_touchPendingY += action.y;
            break;
        case Kind::Wheel: {
            int whole = g_sdlWheel.Add(SdlWheelUnits(action.y, false));
            if (whole) PushEvent(input_state::kRecordWheel, static_cast<uint32_t>(whole));
            break;
        }
        }
    }
    // The game starts the camera drag a frame after the button; motion before that waits instead of being lost.
    if (Dragging() && g_touchCursor && (g_touchPendingX != 0 || g_touchPendingY != 0)) {
        const double scale = TouchDragScale();
        const int dx = g_touchDragX.Add(g_touchPendingX * scale), dy = g_touchDragY.Add(g_touchPendingY * scale);
        g_touchPendingX = g_touchPendingY = 0;
        platform::InjectRelativeMotion(dx, dy);
    }
}

// SDL queues finger events with positions normalized to the window; the gestures work in client pixels, like the mouse.
void ReadSdlTouchEvents()
{
    SDL_Event event;
    SDL_Window* window = SDL_GetWindowFromID(SdlGameWindowId());
    int width = 0, height = 0;
    if (window) SDL_GetWindowSize(window, &width, &height);
    g_touch.SetAim(CurrentTouchAim());
    // Drained even when unused (native hosts), so that the queue does not fill up.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_CANCELED) > 0) {
        if (!kTouchGestures || !g_queueFocused || width <= 0 || height <= 0) continue;
        if (event.type == SDL_EVENT_FINGER_CANCELED) {
            g_touch.Cancel();
            continue;
        }
        if (event.tfinger.windowID != SdlGameWindowId()) continue;
        const auto id = static_cast<int64_t>(event.tfinger.fingerID);
        const float x = event.tfinger.x * static_cast<float>(width), y = event.tfinger.y * static_cast<float>(height);
        const auto ms = static_cast<uint32_t>(event.common.timestamp / 1000000);
        if (event.type == SDL_EVENT_FINGER_DOWN)
            g_touch.FingerDown(id, x, y, ms);
        else if (event.type == SDL_EVENT_FINGER_MOTION)
            g_touch.FingerMove(id, x, y, ms);
        else if (event.type == SDL_EVENT_FINGER_UP)
            g_touch.FingerUp(id, x, y, ms);
    }
    if (!kTouchGestures) return;
    g_touch.Tick(static_cast<uint32_t>(SDL_GetTicks()));
    ApplyTouchActions();
}

// SDL relative drag (docs/build/settings.md): SDL's standard relative mode hides and confines the cursor
// only during a drag and delivers device-unit deltas, as confining the cursor to the anchor would. On release
// the cursor reappears at the grab point. Hidden runs only record the request: SDL would hide, confine and
// warp the user's cursor as soon as the window had keyboard focus. The browser build (kAbsoluteDrag) keeps relative mode
// off: SDL would ask for a pointer lock outside the button's event handler, which the browser grants only on a later
// event. The page takes the lock in the press itself; without it the drag measures the visible pointer's motion.
bool StartSdlRelativeDrag()
{
    SDL_Window* window = SDL_GetWindowFromID(SdlGameWindowId());
    if (!window) return SDL_SetError("no SDL game window");
    if (!g_hidden && !kAbsoluteDrag && !SDL_SetWindowRelativeMouseMode(window, true)) return false;
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    g_absoluteDrag.Start(x, y);
    g_sdlRelativeX.Reset();
    g_sdlRelativeY.Reset();
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION); // Absolute positions queued before entry are not deltas.
    g_sdlRelativeState.requested = true;
    ++g_sdlRelativeState.enables;
    SetLocalCursorDragHidden(true);
    return true;
}

void EndSdlRelativeDrag()
{
    if (!g_sdlRelativeState.requested) return;
    void* gameWindow = GameWindow();
    SIZE size = GameSize();
    const POINT anchor = DragAnchor();
    POINT grab = gameWindow ? MapGameToClient(anchor, size, ClientImageRect(gameWindow, size)) : POINT{-300, -300};
    // After focus loss the physical cursor belongs to another window and stays where the user left it.
    // A page cannot move the pointer: an absolute drag leaves the cursor where the pointer is.
    bool returnCursor = !kAbsoluteDrag && g_inputFocused && anchor.x >= 0 && anchor.y >= 0 && gameWindow;
    // A finger's drag has no mouse position to leave the cursor at (the absolute drag's is the mouse's, stale).
    const bool byFinger = g_touchCursor;
    if (kAbsoluteDrag && g_inputFocused && !byFinger)
        g_sdlMouseClient = {static_cast<LONG>(std::floor(g_absoluteDrag.X())), static_cast<LONG>(std::floor(g_absoluteDrag.Y()))};
    SDL_Window* window = SDL_GetWindowFromID(SdlGameWindowId());
    if (!g_hidden && !kAbsoluteDrag && window) {
        // While the mode is active this warp only moves SDL's hidden position; leaving the mode then makes
        // the single physical warp. Without keyboard focus SDL has already left the mode itself.
        if (returnCursor && SDL_GetKeyboardFocus() == window)
            SDL_WarpMouseInWindow(window, static_cast<float>(grab.x), static_cast<float>(grab.y));
        SDL_SetWindowRelativeMouseMode(window, false);
    }
    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
    g_sdlRelativeX.Reset();
    g_sdlRelativeY.Reset();
    g_sdlRelativeState.requested = false;
    ++g_sdlRelativeState.disables;
    if (returnCursor) {
        // SDL drops the warp's motion event with the flush above; the game reads this position next.
        g_sdlMouseClient = grab;
        ++g_sdlRelativeState.returns;
        g_sdlRelativeState.returnX = grab.x;
        g_sdlRelativeState.returnY = grab.y;
    }
    SetLocalCursorDragHidden(false);
    if (byFinger && !g_touch.AnyFingerDown()) {
        g_sdlMouseClient = {-300, -300};
        g_touchCursor = false;
    }
    g_touchPendingX = g_touchPendingY = 0;
}

// The SDL/Win32 side of the Input object (input_backend.h).
class SdlInputBackend final : public ui::InputBackend {
public:
    bool Install() override
    {
        if (!InitLocalCursor()) return false;
        g_hidden = std::getenv("OBL_HIDDEN") != nullptr;
        // Pin unscaled device units, hidden cursor and warps that only move SDL's relative position.
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0");
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE, "0");
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0");
        // Fingers are the gestures' (touch_gestures.h); SDL must not also turn them into mouse events.
        if (kTouchGestures) SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        return true;
    }

    bool Poll(ui::Input& input) override
    {
        ReadSdlInputEvents(&input);
        return g_inputFocused && (g_hidden || SDL_GetKeyboardFocus() == SDL_GetWindowFromID(SdlGameWindowId()));
    }

    void UpdateKeys(ui::Input& input) override
    {
        ReadSdlInputEvents(&input);
        if (!g_inputFocused) {
            ResetSdlKeyboard();
            memset(input.m_keys, 0, sizeof(input.m_keys));
            return;
        }
        g_keys.Commit(input.m_keys);
    }

    void ResetKeyboard() override { ResetSdlKeyboard(); }

    void* CreateMouseDevice() override
    {
        EnsureEventWatch();
        return &g_mouseDevice;
    }

    MouseRead ReadMouse(ui::Input& input) override
    {
        ReadSdlInputEvents(&input);
        const size_t count = std::min(g_events.size(), std::size(input.m_records));
        std::copy_n(g_events.begin(), count, input.m_records);
        g_events.erase(g_events.begin(), g_events.begin() + static_cast<std::ptrdiff_t>(count));
        POINT pos{};
        // Reuse the backend's mapping directly, independent of the OS cursor API.
        if (g_syntheticCursor)
            pos = g_syntheticClient ? ClientToGame(GameWindow(), g_synthetic) : g_synthetic;
        else
            pos = ClientToGame(GameWindow(), g_sdlMouseClient);
        // The records above were read at the finger's position; with the finger gone the cursor leaves, unless a camera drag is
        // still ending (its release is the game's to process first).
        if (g_touchCursor && !g_touch.AnyFingerDown() && !Dragging()) {
            g_sdlMouseClient = {-300, -300};
            g_touchCursor = false;
        }
        MouseRead read;
        read.count = static_cast<int>(count);
        read.ok = true;
        read.pos = {pos.x, pos.y};
        return read;
    }

    void ReleaseDevices(ui::Input&) override {} // nothing was acquired

    void Reset(bool flushEvents) override
    {
        g_keys.Reset();
        if (flushEvents) {
            SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
            SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
        }
        g_sdlMouseClient = {-300, -300}; // After the drag ends: a reset never returns the cursor position.
        g_sdlWheel.Reset();
        g_events.clear();
        g_touch.Cancel();
        g_touch.Take();
        g_touchCursor = false;
        g_touchPendingX = g_touchPendingY = 0;
    }

    bool Focused() override { return g_inputFocused; }

    void DropRelativeMotion() override
    {
        g_events.erase(std::remove_if(g_events.begin(), g_events.end(),
                               [](const MouseRecord& event) {
                                   return event.offset == input_state::kRecordX || event.offset == input_state::kRecordY;
                               }),
                g_events.end());
    }

    bool StartRelativeDrag() override
    {
        if (StartSdlRelativeDrag()) return true;
        OBL_LOG("input: SDL relative drag could not start: %s", SDL_GetError());
        return false;
    }

    void EndRelativeDrag() override { EndSdlRelativeDrag(); }

    void SetCursorVisible(bool visible) override { SetLocalCursorVisible(visible); }

    void SetCursorShape(ui::Input&, int shape) override { SetLocalCursorShape(shape); }
};

SdlInputBackend g_sdlBackend;

} // namespace

ui::SdlRelativeDragState GetSdlRelativeDragState()
{
    return g_sdlRelativeState;
}

void InjectRelativeMotion(int dx, int dy)
{
    if (!Dragging()) return;
    if (dx) PushEvent(input_state::kRecordX, static_cast<uint32_t>(dx));
    if (dy) PushEvent(input_state::kRecordY, static_cast<uint32_t>(dy));
}

void SetSyntheticCursor(int x, int y)
{
    g_synthetic = {x, y};
    g_syntheticCursor = true;
    g_syntheticClient = false;
}

void SetSyntheticClientCursor(int x, int y)
{
    g_synthetic = {x, y};
    g_syntheticCursor = true;
    g_syntheticClient = true;
}

void InjectMouseButton(int button, bool down)
{
    PushEvent(button == 0 ? input_state::kRecordLeft : input_state::kRecordRight, down ? 0x80 : 0);
}

void ClearSyntheticCursor()
{
    g_syntheticCursor = false;
}

ui::InputBackend* PlatformInputBackend()
{
    return &g_sdlBackend;
}

} // namespace obl::platform
