#pragma once

#include "game/ui/input_state.h"
#include "game/ui/input_types.h"

// The device side of the Input object (docs/architecture/input.md). input_ops.cpp owns the Input entry points (input.h) and their state
// for both hosts; a backend supplies keys, buttons, the cursor position and the system cursor. The Windows host's backend is SDL
// (input.cpp, cursor.cpp); a headless run, and the Linux obl-sim always, has none: every frame reads as no key, button or cursor.
namespace obl::ui {

class InputBackend {
public:
    // Hooks and settings of its own (the window procedure, SDL hints); called once by RegisterInput.
    virtual bool Install() = 0;
    // InputUpdate: reads the pending events; false when the game has no input focus (the object is then reset).
    virtual bool Poll(ui::Input& input) = 0;
    // InputUpdateKeyboard: m_keys for this update.
    virtual void UpdateKeys(ui::Input& input) = 0;
    // The keyboard part of InputInit: drops key state and queued key events.
    virtual void ResetKeyboard() = 0;
    // The mouse part of InputInit: the buffered mouse device the update reads (m_mouseDevice), or null for none.
    virtual void* CreateMouseDevice() = 0;
    // InputUpdateMouse with a device: up to 60 records into m_records. `ok` false = the read failed and is retried next update.
    struct MouseRead {
        bool ok = false;
        int count = 0;
        input_state::Point pos;
    };
    virtual MouseRead ReadMouse(ui::Input& input) = 0;
    // InputDestruct: gives the devices back (the object clears the pointers).
    virtual void ReleaseDevices(ui::Input& input) = 0;
    // The device half of a reset, after the camera drag ended: queued events, latches and the last cursor position.
    virtual void Reset(bool flushEvents) = 0;
    virtual bool Focused() = 0;
    // InputSetRelativeMode: queued relative motion is dropped on every switch; the drag starts after the object entered it
    // (false = it cannot, the object resets) and ends after the object left it.
    virtual void DropRelativeMotion() = 0;
    virtual bool StartRelativeDrag() = 0;
    virtual void EndRelativeDrag() = 0;
    // InputShowSystemCursor/InputHideSystemCursor, InputSetCursorShape (before the object stores the new shape id).
    virtual void SetCursorVisible(bool visible) = 0;
    virtual void SetCursorShape(ui::Input& input, int shape) = 0;

protected:
    ~InputBackend() = default;
};

// The host's backend, or null where it has none (src/host_linux/headless_input.cpp). Not used headless.
} // namespace obl::ui
namespace obl::platform {
ui::InputBackend* PlatformInputBackend();
} // namespace obl::platform
namespace obl::ui {

// The camera drag of InputSetRelativeMode: on, and the cursor position it started at (the game reads it as the position
// while the drag runs).
struct RelativeDrag {
    bool on = false;
    input_state::Point anchor;
};
const RelativeDrag& CurrentRelativeDrag();

// Resets the object and its backend (focus loss, an update without focus); `input` may be null.
void ResetInput(ui::Input* input, bool flushEvents = true);

} // namespace obl::ui
