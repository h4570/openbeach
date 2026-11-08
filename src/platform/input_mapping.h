#pragma once

// Pure physical-key translation and event latching, without SDL calls or game state.

#include "core/key_code.h"

#include <array>
#include <cstdint>

namespace obl::platform {

// [Display] ConfineMouse holds the pointer in the game window so that the screen edge scrolls the map. The in-game options
// (Esc) let it go, so the player reaches other windows without Alt+Tab, and a hidden run never takes the user's mouse.
inline bool WantMouseConfined(bool setting, bool hidden, bool optionsOpen)
{
    return setting && !hidden && !optionsOpen;
}

// Unmapped SDL scancodes return KeyCode::None.
input::KeyCode SdlScancodeToKeyCode(int scancode);

// Key events between keyboard updates, folded into the key-state map.
class KeyLatch {
public:
    void Reset();
    void OnKey(input::KeyCode code, bool down, bool repeat);
    // Writes 256 states (0 released, 1 pressed this frame, 2 held) and starts the next frame.
    void Commit(uint8_t* states);

private:
    std::array<bool, 256> m_down{};
    std::array<bool, 256> m_pressed{};
};

// Fractional device units (wheel notches x 120, relative motion) handed out as whole units, truncated
// toward zero; the remainder carries over, including across direction reversals.
class UnitAccumulator {
public:
    void Reset() { m_remainder = 0; }
    int Add(double units);
    double Remainder() const { return m_remainder; }

private:
    double m_remainder = 0;
};

// A camera drag without SDL relative mode (the browser): the motion of the absolute pointer position since the last event.
// Browsers grant a pointer lock only inside the input event's handler, and the game starts a drag a frame after the button,
// so the page takes the lock itself (web/site.js); this measures the drag while it has none (refused, or left with Esc).
class AbsoluteDrag {
public:
    void Start(float x, float y)
    {
        m_x = x;
        m_y = y;
    }
    struct Delta {
        double x, y;
    };
    Delta Move(float x, float y)
    {
        const Delta delta{static_cast<double>(x) - m_x, static_cast<double>(y) - m_y};
        Start(x, y);
        return delta;
    }
    float X() const { return m_x; }
    float Y() const { return m_y; }

private:
    float m_x = 0, m_y = 0;
};

// True where SDL relative mode cannot work for a drag started outside the button's event handler (the browser).
constexpr bool kAbsoluteDrag =
#ifdef __EMSCRIPTEN__
        true;
#else
        false;
#endif

// Buffered wheel units for one SDL wheel event (SDL reports notches; flipped devices invert them).
double SdlWheelUnits(float notches, bool flipped);

} // namespace obl::platform
