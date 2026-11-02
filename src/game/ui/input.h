#pragma once

#include "game/ui/input_types.h"

#include <cstdint>

// The game side of input: the Input object operations (src/game/ui/input_ops.cpp) and the test injection the platform backend
// implements (src/platform/input.cpp). The Win32 window procedure and message pump stay in platform/input.h.
namespace obl::ui {

// The Input object (input_ops.cpp) with the host's backend, or none when headless (input_backend.h).
bool RegisterInput();

// The Input object's operations (input_ops.cpp; the registered entry points forward to them). The button and cursor getters are the
// fields themselves (m_leftButton, m_rightButton, m_x, m_y). Construct and Destruct register and clear g_Input.
ui::Input* InputConstruct(ui::Input& input, int width, int height, int windowed);
void InputDestruct(ui::Input& input);
void InputSetScreenSize(ui::Input& input, int width, int height);
// `instance` and `window` are the opaque module and window handles of the host.
bool InputInit(ui::Input& input, void* instance, void* window);
bool InputUpdate(ui::Input& input);
bool InputUpdateKeyboard(ui::Input& input);
bool InputUpdateMouse(ui::Input& input);
bool InputKeyHeld(const ui::Input& input, int code);
bool InputKeyPressed(const ui::Input& input, int code);
void InputSetRelativeMode(ui::Input& input, int enable);
void InputShowSystemCursor();
void InputHideSystemCursor();
void InputSetCursorShape(ui::Input& input, int shape);
// Button-state clear, cursor-shape restore and the system cursor switch (zero also hides the system cursor at once).
void InputClearButtonState(ui::Input& input);
void InputRestoreCursorShape(ui::Input& input);
void InputSetSystemCursorEnabled(ui::Input& input, uint8_t enabled);

// Requested SDL relative-mode state of the camera drag. Hidden runs record it without calling SDL,
// so tests can check entry, exit and the cursor's return to the grab point (client coordinates).
struct SdlRelativeDragState {
    bool requested = false;
    unsigned enables = 0, disables = 0, returns = 0;
    int returnX = -300, returnY = -300;
};
} // namespace obl::ui

namespace obl::platform {

ui::SdlRelativeDragState GetSdlRelativeDragState();

// Test injection shares the SDL relative-motion queue path; it never moves the physical cursor.
void InjectRelativeMotion(int dx, int dy);

// Queues a button event for the fallback mouse (test automation; hidden mode always uses it).
void InjectMouseButton(int button, bool down);

// Makes the game see the cursor at (x, y) in game coordinates (test automation, windowed mode).
void SetSyntheticCursor(int x, int y);

// Uses the same client-to-game transform as the physical cursor, without moving the user's mouse.
void SetSyntheticClientCursor(int x, int y);
// Restores the selected real position source after a synthetic cursor fixture.
void ClearSyntheticCursor();

} // namespace obl::platform
