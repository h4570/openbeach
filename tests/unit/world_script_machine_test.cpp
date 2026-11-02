// The scenario script's state machine (src/game/world/world_script_machine.h): which list a runner walks, receipt lookup, loading-bar progress.

#include "unit_test.h"
#include "game/world/world_script_types.h"
#include "game/objects/script_types.h"

#include "game/world/world_script_machine.h"

#include <cmath>
#include <memory>

using namespace obl::script;
using obl::script::ScriptCommand;
using obl::world_script::WorldScript;

namespace {

std::unique_ptr<WorldScript> NewScript()
{
    return std::make_unique<WorldScript>();
}

} // namespace

OBL_TEST_CASE("world_script_machine.lists_come_from_the_given_state")
{
    auto script = NewScript();
    ScriptCommand entry{}, exit{}, update{};
    script->m_states[3].m_onEntry = &entry;
    script->m_states[3].m_onExit = &exit;
    script->m_states[3].m_onUpdate = &update;
    OBL_CHECK(OnEntryList(*script, 3) == &entry);
    OBL_CHECK(OnExitList(*script, 3) == &exit);
    OBL_CHECK(OnUpdateList(*script, 3) == &update);
    OBL_CHECK(OnEntryList(*script, 2) == nullptr);
}

OBL_TEST_CASE("world_script_machine.state_range_is_the_table")
{
    auto script = NewScript();
    OBL_CHECK(StateInRange(*script, 0));
    OBL_CHECK(StateInRange(*script, 9));
    OBL_CHECK(!StateInRange(*script, 10));
    script->m_currentState = 0xffffffffu; // before the script started
    OBL_CHECK(!CurrentStateInRange(*script));
    script->m_currentState = 4;
    OBL_CHECK(CurrentStateInRange(*script));
}

OBL_TEST_CASE("world_script_machine.receipt_takes_the_first_handler_of_the_type")
{
    auto script = NewScript();
    ScriptCommand first{}, second{};
    obl::world_script::WorldScriptState& state = script->m_states[1];
    state.m_NumHandlers = 3;
    state.m_handlers[0] = {7, &first, nullptr};
    state.m_handlers[1] = {9, &second, nullptr};
    state.m_handlers[2] = {9, &first, nullptr}; // a repeated id never wins
    Receipt receipt = FindReceiptList(*script, 1, 9);
    OBL_CHECK(receipt.handled);
    OBL_CHECK(receipt.list == &second);
    receipt = FindReceiptList(*script, 1, 7);
    OBL_CHECK(receipt.handled);
    OBL_CHECK(receipt.list == &first);
    receipt = FindReceiptList(*script, 1, 8);
    OBL_CHECK(!receipt.handled);
    OBL_CHECK(receipt.list == nullptr);
}

OBL_TEST_CASE("world_script_machine.handler_with_an_empty_list_still_handles_the_message")
{
    auto script = NewScript();
    script->m_states[0].m_NumHandlers = 1;
    script->m_states[0].m_handlers[0] = {3, nullptr, nullptr};
    const Receipt receipt = FindReceiptList(*script, 0, 3);
    OBL_CHECK(receipt.handled);
    OBL_CHECK(receipt.list == nullptr);
}

OBL_TEST_CASE("world_script_machine.handlers_beyond_the_count_are_ignored")
{
    auto script = NewScript();
    ScriptCommand node{};
    script->m_states[0].m_NumHandlers = 1;
    script->m_states[0].m_handlers[0] = {1, nullptr, nullptr};
    script->m_states[0].m_handlers[1] = {2, &node, nullptr};
    OBL_CHECK(!FindReceiptList(*script, 0, 2).handled);
    // A message type the table never holds (-1) matches nothing, a state without handlers handles nothing.
    OBL_CHECK(!FindReceiptList(*script, 0, -1).handled);
    OBL_CHECK(!FindReceiptList(*script, 5, 1).handled);
}

OBL_TEST_CASE("world_script_machine.counts_the_commands_of_a_list")
{
    ScriptCommand c{}, b{}, a{};
    a.m_next = &b;
    b.m_next = &c;
    OBL_CHECK_EQ(CountCommands(&a), 3u);
    OBL_CHECK_EQ(CountCommands(&c), 1u);
    OBL_CHECK_EQ(CountCommands(nullptr), 0u);
}

OBL_TEST_CASE("world_script_machine.start_progress_covers_the_last_span_of_the_bar")
{
    // The bar starts at 1 - span and reaches 1 after the last command; a span of 1 covers the whole bar.
    OBL_CHECK_EQ(StartProgress(0, 128, 1.0f), 0.0f);
    OBL_CHECK_EQ(StartProgress(64, 128, 1.0f), 0.5f);
    OBL_CHECK_EQ(StartProgress(0, 128, 0.5f), 0.5f);
    OBL_CHECK_EQ(StartProgress(64, 128, 0.5f), 0.75f);
    OBL_CHECK_EQ(StartProgress(0, 10, 0.0f), 1.0f);
    OBL_CHECK_EQ(StartProgress(5, 10, 0.0f), 1.0f);
}

OBL_TEST_CASE("world_script_machine.start_progress_rounds_the_double_once")
{
    // done / count * span + (1 - span) in double, then one rounding to float (53 bits): not the float arithmetic.
    const float span = 0.3f;
    const double expected = 64.0 / 192.0 * static_cast<double>(span) + (1.0 - static_cast<double>(span));
    OBL_CHECK_EQ(StartProgress(64, 192, span), static_cast<float>(expected));
    OBL_CHECK_EQ(kProgressEvery, 64u);
    OBL_CHECK(std::string(kStartupState) == "STATE_WORLD_STARTUP");
}
