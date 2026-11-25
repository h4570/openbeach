#pragma once

#include "game/mods/input_routing.h"
#include "game/mods/script_runtime.h"
#include "game/ui/input_types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// The Input object (game/ui/input_types.h) as the presentation scripts see and change it: after the input update and before the game
// reads it, the state of this update becomes events (keys, mouse buttons, the pointer, the wheel); an event a mod consumes is taken
// back out of the state, so the game never sees it; and the inputs scripts make up go in like the device's own. Pure logic over the
// object, so unit tests run it with input_state (tests/unit/input_events_test.cpp).
namespace obl::mods {

class InputEvents {
public:
    // The events of this update, in the order: keys by code, mouse records in their order, the pointer, the wheel. Keys and
    // buttons the game must not see because their press was consumed stay hidden while they are held.
    std::vector<InputEvent> Take(ui::Input& input);
    // Takes the event back out of the state, as if the device had not sent it. A key or button press stays hidden until it goes
    // up; a wheel event's notches are taken back. An up, a pointer move or a touch event has nothing to take back.
    void Consume(ui::Input& input, const InputEvent& event);
    // An input a script made up, in the state the game reads in this update.
    void Apply(ui::Input& input, const SyntheticInput& synthetic);
    // Holds the script's pointer, if it has one, after the device's update; the device moving the pointer ends the hold.
    void HoldPointer(ui::Input& input);
    // Whether the device moved the pointer since the script took it (for the last-pointer report).
    bool PointerHeld() const { return m_pointer.has_value(); }
    // Forgets the hidden keys and buttons and the script's pointer (the scripts stopped).
    void Reset();

private:
    std::array<uint8_t, 256> m_keys{}; // the key states of the last update as the device reported them
    std::array<bool, 256> m_hiddenKeys{};
    std::array<bool, 2> m_hiddenButtons{}; // left, right
    std::array<bool, 2> m_pressOpen{}; // the button's last record of this update was a press
    int m_lastX = -300, m_lastY = -300; // the device's pointer of the last update
    std::optional<std::array<int, 2>> m_pointer; // the script's pointer
};

// The scripts' event of a finger event.
InputEvent ToInputEvent(const TouchEvent& touch);

} // namespace obl::mods
