#pragma once
#include <cstdint>
namespace obl {
// Host-installed deadline exchange; null in hosts without a watchdog. Install before starting game work.
using WatchdogLimitExchange = uint32_t (*)(uint32_t);
inline WatchdogLimitExchange g_exchangeWatchdogLimit = nullptr;
class WatchdogLongCommand {
public:
    explicit WatchdogLongCommand(bool longRunning) : m_exchangeLimit(longRunning ? g_exchangeWatchdogLimit : nullptr)
    {
        if (m_exchangeLimit) m_previousLimit = m_exchangeLimit(120000);
    }
    ~WatchdogLongCommand()
    {
        if (m_exchangeLimit) m_exchangeLimit(m_previousLimit);
    }
    WatchdogLongCommand(const WatchdogLongCommand&) = delete;
    WatchdogLongCommand& operator=(const WatchdogLongCommand&) = delete;

private:
    WatchdogLimitExchange m_exchangeLimit;
    uint32_t m_previousLimit = 0;
};
} // namespace obl
