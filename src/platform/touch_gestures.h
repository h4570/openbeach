#pragma once

// Touch gestures for the SDL input backend, without SDL calls or game state: finger events in, mouse-like actions out. The game
// is played with a mouse, so a finger is mapped onto what it uses:
//   tap                    left click
//   drag with one finger   camera drag (the right button held, the motion as deltas)
//   press and hold         left button held until the finger lifts (a slider, a drag in the interface)
//   tap with two fingers   right click
//   drag with two fingers  the wheel
// A finger that has not moved yet only places the cursor; what it turns into is decided by its motion and by time.
// While the GUI places things (TouchAim) one finger is the cursor instead and the map is not dragged.

#include <cmath>
#include <cstdint>
#include <vector>

namespace obl::platform {

// Where the backend handles fingers itself: the browser, which phones and tablets reach. Native hosts keep SDL's touch-to-mouse
// events, so a touch screen there works as before.
constexpr bool kTouchGestures =
#ifdef __EMSCRIPTEN__
        true;
#else
        false;
#endif

// What the GUI wants from a finger, set by the backend from the GUI mode (a gesture keeps the aim it began with).
enum class TouchAim {
    None, // the map: a drag moves the camera
    Confirm, // placing a building or scenery: the finger moves the preview, and lifting it is the press (the game places on a press)
    Paint, // painting grass or paving: the left button is down while the finger drags
};

struct TouchAction {
    enum class Kind {
        Move, // the cursor is at (x, y)
        LeftDown, // at (x, y)
        LeftUp,
        RightDown, // at (x, y); the camera drag starts with it
        RightUp,
        Drag, // camera drag motion since the last one: (x, y) are the deltas in client pixels
        Wheel, // x = 0, y = notches (a finger moving up scrolls down, like a touch list)
    };
    Kind kind;
    float x, y;
};

class TouchGestures {
public:
    // Client pixels and milliseconds. `id` tells the fingers apart.
    void FingerDown(int64_t id, float x, float y, uint32_t ms);
    void FingerMove(int64_t id, float x, float y, uint32_t ms);
    void FingerUp(int64_t id, float x, float y, uint32_t ms);
    // What the GUI is doing now; it applies from the next gesture.
    void SetAim(TouchAim aim) { m_nextAim = aim; }
    // Time passing with the fingers where they are: a finger held still long enough turns into a left button hold.
    void Tick(uint32_t ms);
    // The window lost focus or SDL cancelled the touch: whatever is held goes up.
    void Cancel();

    // The actions since the last call, in order.
    std::vector<TouchAction> Take()
    {
        std::vector<TouchAction> out;
        out.swap(m_out);
        return out;
    }
    bool AnyFingerDown() const { return !m_fingers.empty(); }
    // A camera drag by a finger is running: the right button is down on its behalf.
    bool Panning() const { return m_mode == Mode::Pan; }

    // Motion that still counts as a still finger, the time within which a lift is a tap, and the time after which a still finger
    // is a hold; one wheel notch is this many pixels of two-finger motion.
    static constexpr float kSlop = 14.0f;
    static constexpr uint32_t kTapMs = 350;
    static constexpr uint32_t kHoldMs = 450;
    static constexpr float kPixelsPerNotch = 48.0f;

private:
    enum class Mode {
        Idle,
        One, // one finger down, not decided yet
        Two, // a second finger joined, not decided yet
        Pan, // one finger drags the camera
        Hold, // the left button is held by one finger
        Paint, // the left button is held by one finger for painting (TouchAim::Paint)
        Scroll, // two fingers scroll
        Done, // decided (or too many fingers): nothing more until every finger is up
    };
    struct Finger {
        int64_t id;
        float x, y;
    };

    Finger* Find(int64_t id)
    {
        for (Finger& finger : m_fingers)
            if (finger.id == id) return &finger;
        return nullptr;
    }
    void Emit(TouchAction::Kind kind, float x, float y) { m_out.push_back({kind, x, y}); }
    void Centroid(float& x, float& y) const
    {
        x = y = 0;
        for (const Finger& finger : m_fingers) {
            x += finger.x;
            y += finger.y;
        }
        const float count = static_cast<float>(m_fingers.size());
        x /= count;
        y /= count;
    }
    static float Distance(float ax, float ay, float bx, float by) { return std::hypot(ax - bx, ay - by); }
    void Release()
    {
        // Whatever the gesture holds goes up where its finger was.
        if (m_mode == Mode::Pan) Emit(TouchAction::Kind::RightUp, m_lastX, m_lastY);
        if (m_mode == Mode::Hold || m_mode == Mode::Paint) Emit(TouchAction::Kind::LeftUp, m_lastX, m_lastY);
    }

    Mode m_mode = Mode::Idle;
    std::vector<Finger> m_fingers; // the fingers down, in the order they came
    std::vector<TouchAction> m_out;
    int64_t m_primary = 0; // the finger of One, Pan and Hold
    float m_startX = 0, m_startY = 0; // where the gesture began (the primary finger; the centroid of Two)
    float m_lastX = 0, m_lastY = 0; // the last position the gesture used
    uint32_t m_startMs = 0;
    TouchAim m_nextAim = TouchAim::None;
    TouchAim m_aim = TouchAim::None; // of the gesture under way
};

inline void TouchGestures::FingerDown(int64_t id, float x, float y, uint32_t ms)
{
    if (Find(id)) return;
    m_fingers.push_back({id, x, y});
    if (m_fingers.size() == 1) {
        m_mode = Mode::One;
        m_aim = m_nextAim;
        m_primary = id;
        m_startX = m_lastX = x;
        m_startY = m_lastY = y;
        m_startMs = ms;
        Emit(TouchAction::Kind::Move, x, y);
        if (m_aim == TouchAim::Paint) {
            m_mode = Mode::Paint;
            Emit(TouchAction::Kind::LeftDown, x, y);
        }
    } else if (m_fingers.size() == 2 && m_mode == Mode::One) {
        m_mode = Mode::Two;
        Centroid(m_startX, m_startY);
        m_lastX = m_startX;
        m_lastY = m_startY;
        m_startMs = ms;
    } else if (m_mode == Mode::One || m_mode == Mode::Two) {
        m_mode = Mode::Done; // three fingers are nothing the game has
    }
    // A finger added to a drag or a hold is ignored: the gesture keeps its finger.
}

inline void TouchGestures::FingerMove(int64_t id, float x, float y, uint32_t ms)
{
    Finger* finger = Find(id);
    if (!finger) return;
    finger->x = x;
    finger->y = y;
    switch (m_mode) {
    case Mode::One:
        if (id != m_primary) break;
        if (m_aim == TouchAim::Confirm) {
            // The preview follows the finger; nothing is pressed until it lifts.
            Emit(TouchAction::Kind::Move, x, y);
            m_lastX = x;
            m_lastY = y;
            break;
        }
        if (Distance(x, y, m_startX, m_startY) <= kSlop) break;
        // Moved: a camera drag from where the finger went down, carrying the motion made so far.
        m_mode = Mode::Pan;
        Emit(TouchAction::Kind::Move, m_startX, m_startY);
        Emit(TouchAction::Kind::RightDown, m_startX, m_startY);
        Emit(TouchAction::Kind::Drag, x - m_startX, y - m_startY);
        m_lastX = x;
        m_lastY = y;
        break;
    case Mode::Pan:
        if (id != m_primary) break;
        Emit(TouchAction::Kind::Drag, x - m_lastX, y - m_lastY);
        m_lastX = x;
        m_lastY = y;
        break;
    case Mode::Hold:
    case Mode::Paint:
        if (id != m_primary) break;
        Emit(TouchAction::Kind::Move, x, y);
        m_lastX = x;
        m_lastY = y;
        break;
    case Mode::Two: {
        float cx, cy;
        Centroid(cx, cy);
        if (Distance(cx, cy, m_startX, m_startY) <= kSlop) break;
        m_mode = Mode::Scroll;
        Emit(TouchAction::Kind::Wheel, 0, (cy - m_startY) / kPixelsPerNotch);
        m_lastX = cx;
        m_lastY = cy;
        break;
    }
    case Mode::Scroll: {
        float cx, cy;
        Centroid(cx, cy);
        Emit(TouchAction::Kind::Wheel, 0, (cy - m_lastY) / kPixelsPerNotch);
        m_lastX = cx;
        m_lastY = cy;
        break;
    }
    case Mode::Idle:
    case Mode::Done: break;
    }
    (void)ms;
}

inline void TouchGestures::FingerUp(int64_t id, float x, float y, uint32_t ms)
{
    Finger* finger = Find(id);
    if (!finger) return;
    finger->x = x;
    finger->y = y;
    switch (m_mode) {
    case Mode::One:
        if (id != m_primary) break;
        if (m_aim == TouchAim::Confirm) {
            // The press where the finger left the preview.
            Emit(TouchAction::Kind::Move, x, y);
            Emit(TouchAction::Kind::LeftDown, x, y);
            Emit(TouchAction::Kind::LeftUp, x, y);
            m_mode = Mode::Done;
            break;
        }
        // A finger lifted without having moved is a click, however long it stayed (a hold has its own mode).
        Emit(TouchAction::Kind::Move, m_startX, m_startY);
        Emit(TouchAction::Kind::LeftDown, m_startX, m_startY);
        Emit(TouchAction::Kind::LeftUp, m_startX, m_startY);
        m_mode = Mode::Done;
        break;
    case Mode::Two:
        // Two fingers that touched and left quickly without moving: the right click, where the first one went down.
        if (ms - m_startMs <= kTapMs) {
            Emit(TouchAction::Kind::Move, m_fingers.front().x, m_fingers.front().y);
            Emit(TouchAction::Kind::RightDown, m_fingers.front().x, m_fingers.front().y);
            Emit(TouchAction::Kind::RightUp, m_fingers.front().x, m_fingers.front().y);
        }
        m_mode = Mode::Done;
        break;
    case Mode::Pan:
    case Mode::Hold:
    case Mode::Paint:
        if (id != m_primary) break;
        if (m_mode == Mode::Hold || m_mode == Mode::Paint) {
            Emit(TouchAction::Kind::Move, x, y); // the button goes up where the finger left
            m_lastX = x;
            m_lastY = y;
        }
        Release();
        m_mode = Mode::Done;
        break;
    case Mode::Scroll: m_mode = Mode::Done; break;
    case Mode::Idle:
    case Mode::Done: break;
    }
    for (size_t i = 0; i < m_fingers.size(); ++i) {
        if (m_fingers[i].id != id) continue;
        m_fingers.erase(m_fingers.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    if (m_fingers.empty()) m_mode = Mode::Idle;
}

inline void TouchGestures::Tick(uint32_t ms)
{
    if (m_mode != Mode::One || m_aim != TouchAim::None || ms - m_startMs < kHoldMs) return;
    Finger* finger = Find(m_primary);
    if (!finger) return;
    m_mode = Mode::Hold;
    Emit(TouchAction::Kind::Move, m_startX, m_startY);
    Emit(TouchAction::Kind::LeftDown, m_startX, m_startY);
    m_lastX = m_startX;
    m_lastY = m_startY;
}

inline void TouchGestures::Cancel()
{
    Release();
    m_fingers.clear();
    m_mode = Mode::Idle;
}

} // namespace obl::platform
