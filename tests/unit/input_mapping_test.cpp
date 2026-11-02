// Physical-key translation and event latching for the SDL keyboard/mouse path.

#include "unit_test.h"

#include "platform/input_mapping.h"

// SDL's own Windows table (PC set-1 scancode, E0 prefix packed as 0x80 -> SDL scancode) is an independent
// oracle: our key codes use the same packing, so table[code] must be the SDL key mapped to code.
#include "scancodes_windows.h"

#include <iterator>
#include <set>
#include <string>

using namespace obl::platform;
namespace input = obl::input;

namespace {

// Where the key map and SDL's Windows backend disagree on purpose; everything else must round-trip.
bool KnownDifference(int index, SDL_Scancode sdl)
{
    // SDL packs Pause (E1 1D 45) as 0xE046, which is Ctrl+Break; the key map reports Pause as input::KeyCode::Pause (0xC5).
    if (sdl == SDL_SCANCODE_PAUSE) return index == 0xc6;
    // SDL also accepts the non-extended 0x45 for Num Lock; the key map uses only input::KeyCode::NumLock (0x45).
    if (sdl == SDL_SCANCODE_NUMLOCKCLEAR) return index == 0xc5;
    return false;
}

} // namespace

OBL_TEST_CASE("input.scancode_matches_sdl_windows_table")
{
    static_assert(std::size(windows_scancode_table) == 256);
    int checked = 0;
    for (int index = 1; index < 256; ++index) {
        SDL_Scancode sdl = windows_scancode_table[index];
        if (sdl == SDL_SCANCODE_UNKNOWN || KnownDifference(index, sdl)) continue;
        auto code = SdlScancodeToKeyCode(sdl);
        // Unsupported keys stay unmapped; a mapped key must land on SDL's index.
        if (code && code != index) {
            std::ostringstream msg;
            msg << "SDL scancode " << sdl << " (Windows index 0x" << std::hex << index << ") -> key code 0x" << int(code);
            obl::unit::ReportFailure(__FILE__, __LINE__, msg.str());
        }
        checked += code != 0;
    }
    OBL_CHECK(checked >= 100);
}

OBL_TEST_CASE("input.scancode_every_mapping_is_in_sdl_table")
{
    // Reverse direction: a key code we produce must be an index SDL would decode back to the same key,
    // except for physical aliases that share a key code.
    for (int sdl = 1; sdl < SDL_SCANCODE_COUNT; ++sdl) {
        auto code = SdlScancodeToKeyCode(sdl);
        if (!code) continue;
        SDL_Scancode back = windows_scancode_table[code];
        if (back == sdl) continue;
        bool alias = (sdl == SDL_SCANCODE_NONUSHASH && back == SDL_SCANCODE_BACKSLASH) ||
                (sdl == SDL_SCANCODE_PAUSE && code == input::KeyCode::Pause);
        if (!alias) {
            std::ostringstream msg;
            msg << "SDL scancode " << sdl << " -> key code 0x" << std::hex << int(code) << ", SDL table gives " << std::dec << back;
            obl::unit::ReportFailure(__FILE__, __LINE__, msg.str());
        }
    }
}

OBL_TEST_CASE("input.scancode_keys_the_game_reads")
{
    // Layout-independent physical keys (key code = PC scan set), including extended ones with bit 0x80.
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_A)), input::KeyCode::A);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_Z)), input::KeyCode::Z);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_0)), input::KeyCode::Digit0);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_ESCAPE)), input::KeyCode::Escape);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_F12)), input::KeyCode::F12);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_LEFT)), input::KeyCode::Left);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_KP_ENTER)), input::KeyCode::KeypadEnter);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_RCTRL)), input::KeyCode::RightControl);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_PAUSE)), input::KeyCode::Pause);
    // Non-US keypad keys keep their PC scan-set indices.
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_KP_EQUALS)), 0x59);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_KP_COMMA)), input::KeyCode::KeypadComma);
}

OBL_TEST_CASE("input.scancode_out_of_range")
{
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_UNKNOWN)), 0);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(-1)), 0);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_COUNT)), 0);
    OBL_CHECK_EQ(int(SdlScancodeToKeyCode(SDL_SCANCODE_RESERVED)), 0);
}

OBL_TEST_CASE("input.scancode_distinct_except_aliases")
{
    std::set<int> seen;
    for (int sdl = 1; sdl < SDL_SCANCODE_COUNT; ++sdl) {
        auto code = SdlScancodeToKeyCode(sdl);
        if (!code || sdl == SDL_SCANCODE_NONUSHASH) continue;
        OBL_CHECK(seen.insert(code).second);
    }
}

OBL_TEST_CASE("input.key_latch_states")
{
    uint8_t states[256] = {};
    KeyLatch latch;
    latch.OnKey(input::KeyCode::A, true, false);
    latch.Commit(states);
    OBL_CHECK_EQ(int(states[input::KeyCode::A]), 1); // pressed this frame
    latch.Commit(states);
    OBL_CHECK_EQ(int(states[input::KeyCode::A]), 2); // held
    latch.OnKey(input::KeyCode::A, true, true); // auto-repeat changes nothing
    latch.Commit(states);
    OBL_CHECK_EQ(int(states[input::KeyCode::A]), 2);
    latch.OnKey(input::KeyCode::A, false, false);
    latch.Commit(states);
    OBL_CHECK_EQ(int(states[input::KeyCode::A]), 0);
}

OBL_TEST_CASE("input.key_latch_tap_between_frames")
{
    // Down and up before the game's next update still yields one pressed frame, then released.
    uint8_t states[256] = {};
    KeyLatch latch;
    latch.OnKey(input::KeyCode::Space, true, false);
    latch.OnKey(input::KeyCode::Space, false, false);
    latch.Commit(states);
    OBL_CHECK_EQ(int(states[input::KeyCode::Space]), 1);
    latch.Commit(states);
    OBL_CHECK_EQ(int(states[input::KeyCode::Space]), 0);
}

OBL_TEST_CASE("input.key_latch_ignores_unmapped_and_resets")
{
    uint8_t states[256] = {};
    KeyLatch latch;
    latch.OnKey(input::KeyCode::None, true, false);
    latch.OnKey(input::KeyCode::LeftShift, true, false);
    latch.Reset(); // focus loss
    latch.Commit(states);
    for (uint8_t s : states) OBL_CHECK_EQ(int(s), 0);
    // A down that arrives while already down (no up in between) is not a new press.
    latch.OnKey(input::KeyCode::B, true, false);
    latch.Commit(states);
    latch.OnKey(input::KeyCode::B, true, false);
    latch.Commit(states);
    OBL_CHECK_EQ(int(states[input::KeyCode::B]), 2);
}

OBL_TEST_CASE("input.wheel_units")
{
    OBL_CHECK_EQ(SdlWheelUnits(1.0f, false), 120.0);
    OBL_CHECK_EQ(SdlWheelUnits(-1.0f, false), -120.0);
    OBL_CHECK_EQ(SdlWheelUnits(1.0f, true), -120.0);
    OBL_CHECK_EQ(SdlWheelUnits(0.5f, false), 60.0);
}

OBL_TEST_CASE("input.absolute_drag_deltas")
{
    // The browser's drag: each motion event moves the camera by the pointer's movement since the previous one.
    AbsoluteDrag drag;
    drag.Start(400.0f, 300.0f);
    AbsoluteDrag::Delta d = drag.Move(420.0f, 310.0f);
    OBL_CHECK_EQ(d.x, 20.0);
    OBL_CHECK_EQ(d.y, 10.0);
    d = drag.Move(415.5f, 314.0f);
    OBL_CHECK_EQ(d.x, -4.5);
    OBL_CHECK_EQ(d.y, 4.0);
    OBL_CHECK_EQ(drag.X(), 415.5f);
    // Fractions reach the accumulator whole: half-pixel steps add up.
    UnitAccumulator acc;
    drag.Start(10.0f, 0.0f);
    int total = 0;
    for (int i = 1; i <= 4; ++i) total += acc.Add(drag.Move(10.0f + 0.5f * static_cast<float>(i), 0.0f).x);
    OBL_CHECK_EQ(total, 2);
}

OBL_TEST_CASE("input.accumulator_keeps_fractions")
{
    UnitAccumulator acc;
    // Whole units pass through, fractions carry over (slow relative motion, smooth wheels).
    OBL_CHECK_EQ(acc.Add(0.4), 0);
    OBL_CHECK_EQ(acc.Add(0.4), 0);
    OBL_CHECK_EQ(acc.Add(0.4), 1);
    OBL_CHECK(acc.Remainder() > 0.19 && acc.Remainder() < 0.21);
    // Reversal truncates toward zero and keeps the sign of the remainder.
    OBL_CHECK_EQ(acc.Add(-1.5), -1);
    OBL_CHECK(acc.Remainder() < -0.29 && acc.Remainder() > -0.31);
    OBL_CHECK_EQ(acc.Add(250.5), 250);
    acc.Reset();
    OBL_CHECK_EQ(acc.Remainder(), 0.0);
    OBL_CHECK_EQ(acc.Add(-0.9), 0);
}
