#pragma once

#include <string>
#include <vector>

namespace obl::test {
// sdl-verify: the next SDL present reads the frame back and compares it with the game's buffer.
// posted-key <vk> <0|1>: posts WM_KEYDOWN/WM_KEYUP to the game window (SDL keyboard event counting).
// keyboard-contract: scancode mapping, 0/1/2 states, repeat/tap, modifiers and SDL/Windows focus reset through Input_*.
// window-size / window-place [w h]: window-only sizing; move-client uses the physical cursor transform.
bool ExecutePresenterCommand(const std::string& name, const std::vector<std::string>& args);

// Probes over the SDL presenter: sdl-active, sdl-presents, sdl-texture-width/height, sdl-verified (completed checks),
// sdl-mismatches / sdl-pixels / sdl-bar-pixels / sdl-bar-mismatches (last check), window-width/height,
// display-scale and mouse-x/y (Input fields). SDL window state: sdl-wrapped, sdl-window-width/height,
// sdl-keyboard / keyboard-device, key-<DIK> (0/1/2 state), camera-vx/vy (signed camera velocity),
// window-capture (the game window holds the mouse capture), sdl-keyboard-events (key down/up) / sdl-mouse-motion-events / sdl-mouse-button-events / sdl-other-events (everything else:
// device, window and focus events) = events that reached SDL's filter. False for any other name.
bool ReadPresenterProbe(const std::string& name, double& out);
} // namespace obl::test
