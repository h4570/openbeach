// The pure state of the Input object (src/game/ui/input_state.h), shared by the Windows host and the Linux obl-sim.

#include "unit_test.h"

#include "game/ui/input_state.h"

using namespace obl;
using input_state::Point;

namespace {

ui::Input Windowed()
{
    ui::Input input;
    input_state::Construct(input, 800, 600, 1);
    input.m_x = input.m_y = input_state::kNoCursor;
    return input;
}

void Record(ui::Input& input, int index, uint32_t offset, uint32_t data)
{
    input.m_records[index].offset = offset;
    input.m_records[index].data = data;
}

} // namespace

OBL_TEST_CASE("input_state.construct_clears_everything")
{
    ui::Input input;
    std::memset(&input, 0xcd, sizeof(input));
    input_state::Construct(input, 1024, 768, 0);
    OBL_CHECK_EQ(input.m_width, 1024);
    OBL_CHECK_EQ(input.m_height, 768);
    OBL_CHECK_EQ(input.m_windowed, 0);
    OBL_CHECK(input.m_mouseDevice == nullptr);
    OBL_CHECK_EQ(input.m_keys[255], 0);
    OBL_CHECK_EQ(input.m_cursorShape, 0);
}

OBL_TEST_CASE("input_state.key_states_by_dik")
{
    ui::Input input = Windowed();
    input.m_keys[0x1e] = 1;
    input.m_keys[0x1f] = 2;
    OBL_CHECK(input_state::KeyPressed(input, 0x1e));
    OBL_CHECK(!input_state::KeyHeld(input, 0x1e));
    OBL_CHECK(input_state::KeyHeld(input, 0x1f));
    OBL_CHECK(!input_state::KeyPressed(input, 0x1f));
    OBL_CHECK(!input_state::KeyHeld(input, -1));
    OBL_CHECK(!input_state::KeyPressed(input, 256));
}

OBL_TEST_CASE("input_state.reset_leaves_no_key_button_or_cursor")
{
    ui::Input input = Windowed();
    input.m_keys[3] = 2;
    input.m_leftButton = 2;
    input.m_deltaX = 5;
    input.m_wheel = 120;
    input.m_deferredRightRelease = 1;
    input.m_x = 10;
    input.m_cursorShape = 4;
    input_state::Reset(input);
    OBL_CHECK_EQ(input.m_keys[3], 0);
    OBL_CHECK_EQ(input.m_leftButton, 0);
    OBL_CHECK_EQ(input.m_deltaX, 0);
    OBL_CHECK_EQ(input.m_wheel, 0);
    OBL_CHECK_EQ(input.m_deferredRightRelease, 0);
    OBL_CHECK_EQ(input.m_x, input_state::kNoCursor);
    OBL_CHECK_EQ(input.m_cursorShape, 4); // the shape is not input state
}

OBL_TEST_CASE("input_state.motion_and_window_bounds")
{
    ui::Input input = Windowed();
    input_state::ApplyMouse(input, Point{100, 50}, 0);
    OBL_CHECK_EQ(input.m_x, 100);
    OBL_CHECK_EQ(input.m_y, 50);
    input_state::ApplyMouse(input, Point{110, 45}, 0);
    OBL_CHECK_EQ(input.m_deltaX, 10);
    OBL_CHECK_EQ(input.m_deltaY, -5);
    input_state::ApplyMouse(input, Point{800, 600}, 0);
    OBL_CHECK_EQ(input.m_x, 800);
    input_state::ApplyMouse(input, Point{801, 10}, 0);
    OBL_CHECK_EQ(input.m_x, input_state::kNoCursor);
    OBL_CHECK_EQ(input.m_y, input_state::kNoCursor);
    // Fullscreen clamps to the screen instead.
    input.m_windowed = 0;
    input_state::ApplyMouse(input, Point{900, -4}, 0);
    OBL_CHECK_EQ(input.m_x, 799);
    OBL_CHECK_EQ(input.m_y, 0);
}

OBL_TEST_CASE("input_state.click_within_one_update_is_seen")
{
    ui::Input input = Windowed();
    Record(input, 0, input_state::kRecordLeft, input_state::kButtonDown);
    Record(input, 1, input_state::kRecordLeft, 0);
    input_state::ApplyMouse(input, Point{10, 10}, 2);
    OBL_CHECK_EQ(input.m_leftButton, 1); // pressed, the release waits
    OBL_CHECK_EQ(input.m_deferredLeftRelease, 1);
    input_state::ApplyMouse(input, Point{10, 10}, 0);
    OBL_CHECK_EQ(input.m_leftButton, 0);
    OBL_CHECK_EQ(input.m_deferredLeftRelease, 0);
}

OBL_TEST_CASE("input_state.held_button_and_release")
{
    ui::Input input = Windowed();
    Record(input, 0, input_state::kRecordRight, input_state::kButtonDown);
    input_state::ApplyMouse(input, Point{10, 10}, 1);
    OBL_CHECK_EQ(input.m_rightButton, 1);
    input_state::ApplyMouse(input, Point{10, 10}, 0);
    OBL_CHECK_EQ(input.m_rightButton, 2);
    Record(input, 0, input_state::kRecordRight, 0);
    input_state::ApplyMouse(input, Point{10, 10}, 1);
    OBL_CHECK_EQ(input.m_rightButton, 0);
}

OBL_TEST_CASE("input_state.records_ignored_without_cursor")
{
    ui::Input input = Windowed();
    Record(input, 0, input_state::kRecordLeft, input_state::kButtonDown);
    Record(input, 1, input_state::kRecordWheel, 120);
    input_state::ApplyMouse(input, Point{}, 2);
    OBL_CHECK_EQ(input.m_leftButton, 0);
    OBL_CHECK_EQ(input.m_wheel, 0);
}

OBL_TEST_CASE("input_state.relative_mode_reads_motion_records")
{
    ui::Input input = Windowed();
    input_state::ApplyMouse(input, Point{200, 200}, 0);
    input.m_relativeMode = 1;
    Record(input, 0, input_state::kRecordX, 7);
    Record(input, 1, input_state::kRecordY, static_cast<uint32_t>(-3));
    Record(input, 2, input_state::kRecordX, 2);
    Record(input, 3, input_state::kRecordWheel, 120);
    input_state::ApplyMouse(input, Point{250, 250}, 4); // the position moved, but only the records count as motion
    OBL_CHECK_EQ(input.m_deltaX, 9);
    OBL_CHECK_EQ(input.m_deltaY, -3);
    OBL_CHECK_EQ(input.m_wheel, 120);
    input.m_relativeMode = 0;
    Record(input, 0, input_state::kRecordX, 7);
    input_state::ApplyMouse(input, Point{250, 250}, 1);
    OBL_CHECK_EQ(input.m_deltaX, 0); // absolute mode: motion records are not deltas
}
