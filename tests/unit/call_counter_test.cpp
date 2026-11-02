#include "unit_test.h"

#include "core/log.h"
#include "core/call_counter.h"

#include <deque>
#include <string>
#include <vector>

namespace {
std::vector<std::string> g_counterMessages;
obl::test::CallCounter g_counter("UnitCounterChecks"), g_driverCounter("UnitCounterDriver");
void Drive(int rounds)
{
    for (int i = 0; i < rounds; ++i) g_driverCounter.Count();
}
} // namespace

// Capture the production counter's log through the logger's tap (no log file is open in the unit tests).
namespace {
void Capture(const char* message)
{
    g_counterMessages.emplace_back(message);
}
const bool g_tapped = (obl::log::SetTap(&Capture), true);
} // namespace

OBL_TEST_CASE("call_counter.failed_checks_fail_without_a_report_assertion")
{
    g_counterMessages.clear();
    g_counter.Count();
    g_counter.Record(true, "model agrees");
    OBL_CHECK(g_counterMessages.empty());
    g_counter.Record(false, "model differs");
    OBL_REQUIRE(g_counterMessages.size() == 1);
    OBL_CHECK(g_counterMessages.front().starts_with("autotest: FAILED counter-check UnitCounterChecks"));
    OBL_CHECK(g_counterMessages.front().find("model differs") != std::string::npos);
    OBL_CHECK_EQ(g_counter.Calls(), 3u);
    OBL_CHECK(obl::test::ExecuteCallCounterCommand("call-report", {"UnitCounterChecks"}));
    OBL_CHECK_EQ(g_counterMessages.back(), "autotest: call-report UnitCounterChecks calls=3");
    OBL_CHECK(!obl::test::ExecuteCallCounterCommand("shadow", {"reset"}));
    OBL_CHECK(!obl::test::ExecuteCallCounterCommand("shadow-report", {"UnitCounterChecks"}));
    OBL_CHECK_EQ(g_counter.Calls(), 3u);
    for (int i = 0; i < 8; ++i) g_counter.Record(false, "more model differences");
    OBL_CHECK_EQ(g_counterMessages.back(), "autotest: FAILED counter-check UnitCounterChecks #9");
    OBL_CHECK_EQ(g_counter.Calls(), 11u);
}

OBL_TEST_CASE("call_counter.drivers_and_invalid_commands")
{
    g_counterMessages.clear();
    g_driverCounter.SetDriver(Drive);
    OBL_CHECK(obl::test::ExecuteCallCounterCommand("call-drive", {"UnitCounterDriver", "4"}));
    OBL_CHECK_EQ(g_driverCounter.Calls(), 4u);
    OBL_CHECK_EQ(g_counterMessages.back(), "autotest: call-drive UnitCounterDriver 4 calls-delta=4");
    OBL_CHECK(obl::test::ExecuteCallCounterCommand("call-report", {"MissingCounter"}));
    OBL_CHECK(g_counterMessages.back().starts_with("autotest: FAILED call-report"));
    OBL_CHECK(obl::test::ExecuteCallCounterCommand("call-drive", {"UnitCounterChecks", "1"}));
    OBL_CHECK(g_counterMessages.back().starts_with("autotest: FAILED call-drive"));
}

namespace {
obl::test::CallCounter g_growingDriverCounter("UnitCounterGrowingDriver");
// Function-local counters register on first use, so a driver can grow the registry while call-drive runs it.
void DriveAndRegister(int rounds)
{
    static std::deque<obl::test::CallCounter> late;
    for (int i = 0; i < 64; ++i) late.emplace_back("UnitCounterLate");
    for (int i = 0; i < rounds; ++i) g_growingDriverCounter.Count();
}
} // namespace

OBL_TEST_CASE("call_counter.driver_may_register_counters")
{
    g_counterMessages.clear();
    g_growingDriverCounter.SetDriver(DriveAndRegister);
    OBL_CHECK(obl::test::ExecuteCallCounterCommand("call-drive", {"UnitCounterGrowingDriver", "2"}));
    OBL_CHECK_EQ(g_growingDriverCounter.Calls(), 2u);
    OBL_CHECK_EQ(g_counterMessages.back(), "autotest: call-drive UnitCounterGrowingDriver 2 calls-delta=2");
}
