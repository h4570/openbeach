#include "core/fatal.h"

#include <cstdio>
#include <cstdlib>

namespace obl::fatal {
namespace {
ReportHandlers g_handlers;
}
void SetReportHandlers(ReportHandlers handlers)
{
    g_handlers = handlers;
}
std::string DescribeAddress(uintptr_t address)
{
    if (g_handlers.describeAddress) return g_handlers.describeAddress(address);
    char text[2 * sizeof(uintptr_t) + 1];
    std::snprintf(text, sizeof(text), "%zx", static_cast<size_t>(address));
    return text;
}
void UnhandledInput(const char* function, const char* reason)
{
    if (g_handlers.unhandledInput) g_handlers.unhandledInput(function, reason);
    std::abort();
}
} // namespace obl::fatal
