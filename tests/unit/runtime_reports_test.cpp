#include "core/fatal.h"
#include "core/watchdog.h"
#include "unit_test.h"

namespace {
uint32_t g_limit = 20000;
int g_changes = 0;
uint32_t ExchangeLimit(uint32_t limit)
{
    ++g_changes;
    auto previous = g_limit;
    g_limit = limit;
    return previous;
}
} // namespace
OBL_TEST_CASE("Runtime report handler describes addresses and can be cleared")
{
    obl::fatal::SetReportHandlers({[](uintptr_t address) { return std::to_string(address); }, nullptr});
    OBL_CHECK_EQ(obl::fatal::DescribeAddress(42), std::string("42"));
    obl::fatal::SetReportHandlers({});
    OBL_CHECK_EQ(obl::fatal::DescribeAddress(42), std::string("2a"));
}
OBL_TEST_CASE("Watchdog long scopes restore nested deadlines and ignore inactive scopes")
{
    obl::g_exchangeWatchdogLimit = ExchangeLimit;
    g_changes = 0;
    g_limit = 20000;
    {
        obl::WatchdogLongCommand outer(true);
        OBL_CHECK_EQ(g_limit, 120000u);
        {
            obl::WatchdogLongCommand inner(true);
        }
        OBL_CHECK_EQ(g_limit, 120000u);
        {
            obl::WatchdogLongCommand inactive(false);
        }
        OBL_CHECK_EQ(g_changes, 3);
    }
    OBL_CHECK_EQ(g_limit, 20000u);
    OBL_CHECK_EQ(g_changes, 4);
    obl::g_exchangeWatchdogLimit = nullptr;
    {
        obl::WatchdogLongCommand noHost(true);
    }
    OBL_CHECK_EQ(g_changes, 4);
}
