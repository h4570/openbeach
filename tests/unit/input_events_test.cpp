// The Input object as presentation scripts see and change it (src/game/mods/input_events.h): events from an update, consumed events
// taken back out before the game reads them, inputs scripts make up, the script's pointer, and finger events.

#include "unit_test.h"

#include "core/key_code.h"
#include "game/mods/input_events.h"
#include "game/ui/input_state.h"

#include <initializer_list>
#include <string>
#include <vector>

using namespace obl;

namespace {

struct Record {
    uint32_t offset, data;
};

ui::Input NewInput()
{
    ui::Input input{};
    input_state::Construct(input, 800, 600, 1);
    input_state::InitMouse(input);
    return input;
}

// What InputUpdateMouse does after the device read `records` with the cursor at (x, y).
void Update(ui::Input& input, int x, int y, std::initializer_list<Record> records = {})
{
    int count = 0;
    for (const Record& record : records) input.m_records[count++] = {record.offset, record.data, 0, 0, 0};
    input.m_recordCount = count;
    input.m_wheel = 0;
    input_state::ApplyMouse(input, {x, y}, count);
}

std::vector<std::string> Describe(const std::vector<mods::InputEvent>& events)
{
    std::vector<std::string> lines;
    for (const auto& e : events) {
        std::string line = e.kind + " " + e.phase;
        if (!e.key.empty()) line += " " + e.key;
        if (!e.button.empty()) line += " " + e.button;
        if (e.kind != "key") line += " " + std::to_string(e.x) + "," + std::to_string(e.y);
        if (e.kind == "wheel") line += " " + std::to_string(static_cast<int>(e.delta));
        lines.push_back(line);
    }
    return lines;
}

constexpr uint32_t kLeft = input_state::kRecordLeft, kRight = input_state::kRecordRight, kDown = input_state::kButtonDown;
constexpr int kF5 = static_cast<int>(input::KeyCode::F5);

} // namespace

OBL_TEST_CASE("input_events.keys_go_down_and_up_and_a_consumed_press_stays_hidden_until_released")
{
    mods::InputEvents events;
    ui::Input input = NewInput();
    Update(input, 100, 100);
    events.Take(input); // the first pointer position
    input.m_keys[kF5] = 1;
    const auto down = events.Take(input);
    OBL_CHECK(Describe(down) == std::vector<std::string>{"key down F5"});
    events.Consume(input, down[0]);
    OBL_CHECK_EQ(input.m_keys[kF5], 0);
    input.m_keys[kF5] = 2; // held: the device's state, the game must still not see it
    OBL_CHECK(events.Take(input).empty());
    OBL_CHECK_EQ(input.m_keys[kF5], 0);
    input.m_keys[kF5] = 0;
    OBL_CHECK(Describe(events.Take(input)) == std::vector<std::string>{"key up F5"});
    input.m_keys[kF5] = 1; // pressed again, not consumed: the game sees it
    events.Take(input);
    OBL_CHECK_EQ(input.m_keys[kF5], 1);
}

OBL_TEST_CASE("input_events.a_consumed_click_never_reaches_the_buttons_the_game_reads")
{
    mods::InputEvents events;
    ui::Input input = NewInput();
    Update(input, 100, 100);
    events.Take(input);
    // Press and release in one update: the game would see the press now and the release next update.
    Update(input, 120, 130, {{kLeft, kDown}, {kLeft, 0}});
    const auto clicked = events.Take(input);
    OBL_CHECK(Describe(clicked) == (std::vector<std::string>{"mouse down left 120,130", "mouse up left 120,130", "pointer move 120,130"}));
    events.Consume(input, clicked[0]);
    OBL_CHECK_EQ(input.m_leftButton, 0);
    Update(input, 120, 130);
    events.Take(input);
    OBL_CHECK_EQ(input.m_leftButton, 0);
    // A press held over updates, consumed: hidden until its release, which the game then also does not see.
    Update(input, 120, 130, {{kRight, kDown}});
    const auto held = events.Take(input);
    events.Consume(input, held[0]);
    Update(input, 120, 130);
    events.Take(input);
    OBL_CHECK_EQ(input.m_rightButton, 0);
    Update(input, 120, 130, {{kRight, 0}});
    OBL_CHECK(Describe(events.Take(input)) == std::vector<std::string>{"mouse up right 120,130"});
    OBL_CHECK_EQ(input.m_rightButton, 0);
    // Not consumed, the next press reaches the game.
    Update(input, 120, 130, {{kRight, kDown}});
    events.Take(input);
    OBL_CHECK_EQ(input.m_rightButton, 1);
}

OBL_TEST_CASE("input_events.a_made_up_click_is_a_press_in_this_update_and_a_release_in_the_next")
{
    mods::InputEvents events;
    ui::Input input = NewInput();
    Update(input, -300, -300); // no device pointer, as with touch taken over
    events.Take(input);
    events.Apply(input, {mods::SyntheticInput::Kind::Click, 1, 300, 200});
    OBL_CHECK_EQ(input.m_rightButton, 1);
    OBL_CHECK_EQ(input.m_x, 300);
    OBL_CHECK_EQ(input.m_y, 200);
    // The next update: the device still has no pointer, the script's holds, and the deferred release lands.
    Update(input, -300, -300);
    OBL_CHECK(events.Take(input).empty()); // made-up input is not handed back to the scripts
    events.HoldPointer(input);
    OBL_CHECK_EQ(input.m_rightButton, 0);
    OBL_CHECK_EQ(input.m_x, 300);
    // Press and release over several updates hold the button in between.
    events.Apply(input, {mods::SyntheticInput::Kind::Press, 0, 310, 210});
    OBL_CHECK_EQ(input.m_leftButton, 1);
    Update(input, -300, -300);
    events.Take(input);
    events.HoldPointer(input);
    OBL_CHECK_EQ(input.m_leftButton, 2);
    events.Apply(input, {mods::SyntheticInput::Kind::Release, 0, 310, 210});
    OBL_CHECK_EQ(input.m_leftButton, 0);
}

OBL_TEST_CASE("input_events.the_script_pointer_holds_until_the_device_moves_or_the_script_frees_it")
{
    mods::InputEvents events;
    ui::Input input = NewInput();
    Update(input, 100, 100);
    events.Take(input);
    events.Apply(input, {mods::SyntheticInput::Kind::Move, 0, 400, 300});
    OBL_CHECK(events.PointerHeld());
    Update(input, 100, 100); // the device did not move
    events.Take(input);
    events.HoldPointer(input);
    OBL_CHECK_EQ(input.m_x, 400);
    Update(input, 150, 100); // it did
    OBL_CHECK(Describe(events.Take(input)) == std::vector<std::string>{"pointer move 150,100"});
    events.HoldPointer(input);
    OBL_CHECK_EQ(input.m_x, 150);
    OBL_CHECK(!events.PointerHeld());
    events.Apply(input, {mods::SyntheticInput::Kind::Move, 0, 10, 20});
    events.Apply(input, {mods::SyntheticInput::Kind::Free, 0, 0, 0});
    OBL_CHECK_EQ(input.m_x, 150);
    OBL_CHECK(!events.PointerHeld());
}

OBL_TEST_CASE("input_events.the_wheel_is_one_event_in_notches_and_consuming_takes_them_back")
{
    mods::InputEvents events;
    ui::Input input = NewInput();
    Update(input, 100, 100);
    events.Take(input);
    Update(input, 100, 100, {{input_state::kRecordWheel, 120}, {input_state::kRecordWheel, 120}});
    OBL_CHECK_EQ(input.m_wheel, 240);
    const auto wheel = events.Take(input);
    OBL_CHECK(Describe(wheel) == std::vector<std::string>{"wheel move 100,100 2"});
    events.Consume(input, wheel[0]);
    OBL_CHECK_EQ(input.m_wheel, 0);
}

OBL_TEST_CASE("input_events.records_without_a_cursor_are_not_events")
{
    mods::InputEvents events;
    ui::Input input = NewInput();
    Update(input, -300, -300, {{kLeft, kDown}});
    OBL_CHECK(events.Take(input).empty()); // the game ignored them too
}

OBL_TEST_CASE("input_events.finger_events_keep_their_finger_and_phase")
{
    const mods::InputEvent down = mods::ToInputEvent({7, mods::TouchPhase::Down, 12, 34});
    OBL_CHECK_EQ(down.kind, std::string("touch"));
    OBL_CHECK_EQ(down.phase, std::string("down"));
    OBL_CHECK_EQ(down.finger, int64_t{7});
    OBL_CHECK_EQ(down.x, 12);
    OBL_CHECK_EQ(down.y, 34);
    OBL_CHECK_EQ(mods::ToInputEvent({1ll << 40, mods::TouchPhase::Move, 0, 0}).finger, int64_t{1} << 40);
    OBL_CHECK_EQ(mods::ToInputEvent({1, mods::TouchPhase::Up, 0, 0}).phase, std::string("up"));
    OBL_CHECK_EQ(mods::ToInputEvent({1, mods::TouchPhase::Cancel, 0, 0}).phase, std::string("cancel"));
}
