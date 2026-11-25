#include "game/mods/input_events.h"

#include "game/mods/key_names.h"
#include "game/ui/input_state.h"

namespace obl::mods {

namespace {

constexpr int kWheelUnitsPerNotch = 120; // the record unit of a wheel notch (input_state.h)

int& Button(ui::Input& input, int button)
{
    return button == 0 ? input.m_leftButton : input.m_rightButton;
}

uint8_t& DeferredRelease(ui::Input& input, int button)
{
    return button == 0 ? input.m_deferredLeftRelease : input.m_deferredRightRelease;
}

int ButtonOf(const InputEvent& event)
{
    return event.button == "right" ? 1 : 0;
}

uint8_t KeyCode(const std::string& name)
{
    for (int code = 1; code < 256; ++code)
        if (KeyName(static_cast<uint8_t>(code)) == name) return static_cast<uint8_t>(code);
    return 0;
}

} // namespace

std::vector<InputEvent> InputEvents::Take(ui::Input& input)
{
    std::vector<InputEvent> events;
    for (int code = 1; code < 256; ++code) {
        const uint8_t now = input.m_keys[code], before = m_keys[code];
        m_keys[code] = now;
        const std::string_view name = KeyName(static_cast<uint8_t>(code));
        // 1 is "pressed in this update"; a key that was down and reads 0 now was released.
        if (!name.empty() && (now == 1 || (now == 0 && before != 0))) {
            InputEvent event;
            event.kind = "key";
            event.phase = now ? "down" : "up";
            event.key = std::string(name);
            events.push_back(std::move(event));
        }
        if (m_hiddenKeys[code]) {
            if (now)
                input.m_keys[code] = 0;
            else
                m_hiddenKeys[code] = false;
        }
    }
    // The records count only where the update applied them: with no cursor the game ignored them too.
    const bool applied = input.m_x != input_state::kNoCursor && input.m_y != input_state::kNoCursor;
    int wheel = 0;
    m_pressOpen = {};
    for (int i = 0; applied && i < input.m_recordCount && i < static_cast<int>(std::size(input.m_records)); ++i) {
        const ui::Input::MouseRecord& record = input.m_records[i];
        if (record.offset == input_state::kRecordWheel) {
            wheel += static_cast<int>(record.data);
            continue;
        }
        if (record.offset != input_state::kRecordLeft && record.offset != input_state::kRecordRight) continue;
        const int button = record.offset == input_state::kRecordLeft ? 0 : 1;
        const bool down = (record.data & input_state::kButtonDown) != 0;
        if (!down) m_hiddenButtons[button] = false;
        m_pressOpen[button] = down;
        InputEvent event;
        event.kind = "mouse";
        event.phase = down ? "down" : "up";
        event.button = button == 0 ? "left" : "right";
        event.x = input.m_x;
        event.y = input.m_y;
        events.push_back(std::move(event));
    }
    for (int button = 0; button < 2; ++button)
        if (m_hiddenButtons[button]) {
            Button(input, button) = 0;
            DeferredRelease(input, button) = 0;
        }
    if (applied && !input.m_relativeMode && (input.m_x != m_lastX || input.m_y != m_lastY)) {
        InputEvent event;
        // Its own kind, so a script that reads `button` of every mouse event never meets a move without one.
        event.kind = "pointer";
        event.phase = "move";
        event.x = input.m_x;
        event.y = input.m_y;
        events.push_back(std::move(event));
        m_pointer.reset(); // the device moved the pointer: it is the device's again
    }
    m_lastX = input.m_x;
    m_lastY = input.m_y;
    if (wheel != 0) {
        InputEvent event;
        event.kind = "wheel";
        event.phase = "move";
        event.x = input.m_x;
        event.y = input.m_y;
        event.delta = static_cast<double>(wheel) / kWheelUnitsPerNotch;
        events.push_back(std::move(event));
    }
    return events;
}

void InputEvents::Consume(ui::Input& input, const InputEvent& event)
{
    if (event.phase != "down" && event.kind != "wheel") return;
    if (event.kind == "key") {
        const uint8_t code = KeyCode(event.key);
        if (code == 0) return;
        m_hiddenKeys[code] = true;
        input.m_keys[code] = 0;
    } else if (event.kind == "mouse") {
        const int button = ButtonOf(event);
        // A press whose release came in the same update has nothing left to hide.
        m_hiddenButtons[button] = m_pressOpen[button];
        Button(input, button) = 0;
        DeferredRelease(input, button) = 0;
    } else if (event.kind == "wheel") {
        input.m_wheel = 0; // one wheel event holds the update's every notch
    }
}

void InputEvents::Apply(ui::Input& input, const SyntheticInput& synthetic)
{
    using Kind = SyntheticInput::Kind;
    if (synthetic.kind == Kind::Free) {
        m_pointer.reset();
        input.m_x = m_lastX;
        input.m_y = m_lastY;
        return;
    }
    // A press or release also holds the pointer where it happened, so the game reads the release where the press was.
    m_pointer = std::array<int, 2>{synthetic.x, synthetic.y};
    input.m_x = synthetic.x;
    input.m_y = synthetic.y;
    if (synthetic.kind == Kind::Move) return;
    int& button = Button(input, synthetic.button);
    if (synthetic.kind == Kind::Press || synthetic.kind == Kind::Click) {
        m_hiddenButtons[synthetic.button] = false;
        if (!button) button = 1;
    }
    // The same rule as a device release (input_state::ApplyMouse): a release in the update of the press waits for the next one.
    if (synthetic.kind == Kind::Release || synthetic.kind == Kind::Click) {
        if (button == 1)
            DeferredRelease(input, synthetic.button) = 1;
        else
            button = 0;
    }
}

void InputEvents::HoldPointer(ui::Input& input)
{
    if (!m_pointer) return;
    input.m_x = (*m_pointer)[0];
    input.m_y = (*m_pointer)[1];
}

void InputEvents::Reset()
{
    m_hiddenKeys = {};
    m_hiddenButtons = {};
    m_pointer.reset();
}

InputEvent ToInputEvent(const TouchEvent& touch)
{
    static constexpr const char* kPhases[] = {"down", "move", "up", "cancel"};
    InputEvent event;
    event.kind = "touch";
    event.phase = kPhases[static_cast<int>(touch.phase)];
    event.finger = touch.finger;
    event.x = touch.x;
    event.y = touch.y;
    return event;
}

} // namespace obl::mods
