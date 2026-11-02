// The Input object for both hosts (docs/architecture/input.md): every Input entry point of input.h and the cursor entry points, over the pure
// state in src/game/ui/input_state.h and an InputBackend for the devices. Portable: the Linux obl-sim builds it as is.

#include "game/ui/input.h"

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "game/ui/input_backend.h"
#include "game/ui/input_state.h"

#include <cstring>

namespace obl::ui {
namespace {

using ui::Input;

// A run without devices: no events, no focus, no mouse device, no system cursor; a camera drag cannot start.
class NoInputBackend final : public InputBackend {
public:
    bool Install() override { return true; }
    bool Poll(Input&) override { return false; }
    void UpdateKeys(Input& input) override { std::memset(input.m_keys, 0, sizeof(input.m_keys)); }
    void ResetKeyboard() override {}
    void* CreateMouseDevice() override { return nullptr; }
    MouseRead ReadMouse(Input&) override { return {}; }
    void ReleaseDevices(Input&) override {}
    void Reset(bool) override {}
    bool Focused() override { return true; }
    void DropRelativeMotion() override {}
    bool StartRelativeDrag() override { return false; }
    void EndRelativeDrag() override {}
    void SetCursorVisible(bool) override {}
    void SetCursorShape(Input&, int) override {}
};

NoInputBackend g_none;
InputBackend* g_backend = &g_none;
RelativeDrag g_drag;

void EndDrag(Input* input)
{
    g_drag.on = false;
    if (input) input->m_relativeMode = 0;
    g_backend->EndRelativeDrag();
}

bool ResetKeyboardState(Input* input)
{
    g_backend->ResetKeyboard();
    // A null device is valid for the game destructor. No keyboard device is acquired.
    input->m_keyboardDevice = nullptr;
    std::memset(input->m_keys, 0, sizeof(input->m_keys));
    return true;
}

bool OpenMouse(Input* input)
{
    input->m_mouseDevice = g_backend->CreateMouseDevice();
    input_state::InitMouse(*input);
    return true;
}

bool ReadKeyboard(Input* input)
{
    g_backend->UpdateKeys(*input);
    return true;
}

bool ReadMouse(Input* input)
{
    if (!input->m_mouseDevice) return false;
    const InputBackend::MouseRead read = g_backend->ReadMouse(*input);
    input->m_recordCount = read.count;
    input->m_wheel = 0;
    if (!read.ok) return true;
    input_state::ApplyMouse(*input, g_drag.on ? g_drag.anchor : read.pos, read.count);
    return true;
}

} // namespace

Input* InputConstruct(Input& input, int width, int height, int windowed)
{
    input_state::Construct(input, width, height, windowed);
    game::SetInput(&input);
    return &input;
}

void InputSetScreenSize(Input& input, int width, int height)
{
    input.m_width = width;
    input.m_height = height;
}

bool InputInit(Input& input, void* instance, void* window)
{
    input.m_window = window;
    input.m_module = instance;
    return ResetKeyboardState(&input) && OpenMouse(&input);
}

bool InputUpdateKeyboard(Input& input)
{
    return ReadKeyboard(&input);
}

bool InputUpdateMouse(Input& input)
{
    return ReadMouse(&input);
}

bool InputUpdate(Input& input)
{
    if (!g_backend->Poll(input)) {
        ResetInput(&input);
        return true;
    }
    return ReadKeyboard(&input) && ReadMouse(&input);
}

void InputDestruct(Input& input)
{
    ResetInput(&input);
    g_backend->ReleaseDevices(input);
    input.m_keyboardDevice = input.m_mouseDevice = input.m_directInput = nullptr;
    game::SetInput(nullptr);
}

bool InputKeyHeld(const Input& input, int code)
{
    return input_state::KeyHeld(input, code);
}

bool InputKeyPressed(const Input& input, int code)
{
    return input_state::KeyPressed(input, code);
}

// InputSetRelativeMode: the camera drag. The cursor position is frozen at the anchor and
// the update reads relative motion as the deltas.
void InputSetRelativeMode(Input& input, int enable)
{
    const bool on = (enable & 0xff) != 0;
    if (on == g_drag.on) return;
    if (on && !g_backend->Focused()) return;
    g_backend->DropRelativeMotion();
    input.m_deltaX = 0;
    input.m_deltaY = 0;
    if (!on) {
        EndDrag(&input);
        return;
    }
    g_drag.anchor = {input.m_x, input.m_y};
    input.m_relativeMode = 1;
    g_drag.on = true;
    if (!g_backend->StartRelativeDrag()) ResetInput(&input);
}

void InputShowSystemCursor()
{
    g_backend->SetCursorVisible(true);
}

void InputHideSystemCursor()
{
    g_backend->SetCursorVisible(false);
}

void InputSetCursorShape(Input& input, int shape)
{
    if (input.m_cursorShape == shape) return;
    g_backend->SetCursorShape(input, shape);
    input.m_cursorShape = shape;
}

void InputClearButtonState(Input& input)
{
    input.m_leftButton = 0;
    input.m_rightButton = 0;
    input.m_deferredLeftRelease = 0;
    input.m_deferredRightRelease = 0;
}

void InputRestoreCursorShape(Input& input)
{
    InputSetCursorShape(input, input.m_cursorShape);
}

// Stores the byte as given.
void InputSetSystemCursorEnabled(Input& input, uint8_t enabled)
{
    input.m_systemCursorEnabled = enabled;
    if (!enabled) InputHideSystemCursor();
}

const RelativeDrag& CurrentRelativeDrag()
{
    return g_drag;
}

void ResetInput(Input* input, bool flushEvents)
{
    EndDrag(input);
    g_backend->Reset(flushEvents);
    if (input) input_state::Reset(*input);
}

bool RegisterInput()
{
    // Headless runs read no devices on either host (docs/architecture/headless/index.md).
    InputBackend* platform = config::Get().headless ? nullptr : platform::PlatformInputBackend();
    g_backend = platform ? platform : &g_none;
    return g_backend->Install();
}

} // namespace obl::ui
