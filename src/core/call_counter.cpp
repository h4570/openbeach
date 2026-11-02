#include "call_counter.h"

#include "core/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace obl::test {
namespace {

constexpr uint64_t kLoggedFailures = 8;

std::vector<CallCounter*>& Registry()
{
    static std::vector<CallCounter*> registry;
    return registry;
}

} // namespace

CallCounter::CallCounter(const char* name) : m_name(name)
{
    Registry().push_back(this);
}

void CallCounter::Record(bool ok, const std::string& detail)
{
    ++m_calls;
    if (ok) return;
    uint64_t n = ++m_failures;
    if (n <= kLoggedFailures)
        OBL_LOG("autotest: FAILED counter-check %s #%llu: %s", m_name, static_cast<unsigned long long>(n), detail.c_str());
    else
        OBL_LOG("autotest: FAILED counter-check %s #%llu", m_name, static_cast<unsigned long long>(n));
}

bool SetCallDriver(const char* name, CallCounter::Driver driver)
{
    auto it = std::find_if(Registry().begin(), Registry().end(), [&](const CallCounter* f) { return std::strcmp(name, f->Name()) == 0; });
    if (it == Registry().end()) return false;
    (*it)->SetDriver(driver);
    return true;
}

void ResetCallCounters()
{
    for (CallCounter* counter : Registry()) counter->Reset();
}

bool ExecuteCallCounterCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "call-drive") {
        auto it = args.size() == 2
                ? std::find_if(Registry().begin(), Registry().end(), [&](const CallCounter* f) { return args[0] == f->Name(); })
                : Registry().end();
        if (it == Registry().end() || !(*it)->GetDriver()) {
            OBL_LOG("autotest: FAILED call-drive: expected <function with a driver> <count>");
            return true;
        }
        // A driver may construct new counters (function-local statics), which grows the registry and invalidates `it`.
        CallCounter* counter = *it;
        const uint64_t before = counter->Calls();
        counter->GetDriver()(std::atoi(args[1].c_str()));
        OBL_LOG("autotest: call-drive %s %s calls-delta=%llu", args[0].c_str(), args[1].c_str(),
                static_cast<unsigned long long>(counter->Calls() - before));
        return true;
    }
    if (name != "call-report") return false;
    if (args.size() != 1) {
        OBL_LOG("autotest: FAILED call-report: expected <function>");
        return true;
    }
    auto it = std::find_if(Registry().begin(), Registry().end(), [&](const CallCounter* f) { return args[0] == f->Name(); });
    if (it == Registry().end()) {
        OBL_LOG("autotest: FAILED call-report: unrecognized function '%s'", args[0].c_str());
        return true;
    }
    const CallCounter& f = **it;
    OBL_LOG("autotest: call-report %s calls=%llu", f.Name(), static_cast<unsigned long long>(f.Calls()));
    return true;
}

} // namespace obl::test
