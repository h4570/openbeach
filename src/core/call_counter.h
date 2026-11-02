#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Named call counters and golden/model checks of C++ functions (docs/testing/call-counters.md).
namespace obl::test {

class CallCounter {
public:
    // Optional driver for functions the default game path never reaches (`call-drive`): calls the counted function `count` times
    // with synthetic inputs.
    using Driver = void (*)(int count);

    explicit CallCounter(const char* name);
    CallCounter(const char* name, Driver driver) : CallCounter(name) { m_driver = driver; }

    const char* Name() const { return m_name; }
    uint64_t Calls() const { return m_calls; }
    // Counts one call.
    void Count() { ++m_calls; }
    // Counts one call that also checked its result against a golden value or a model. On a failure the first few `detail` strings are
    // logged as autotest failures. A function that checks nothing calls Count().
    void Record(bool ok, const std::string& detail);

    void SetDriver(Driver driver) { m_driver = driver; }
    Driver GetDriver() const { return m_driver; }
    void Reset()
    {
        m_calls = 0;
        m_failures = 0;
    }

private:
    const char* m_name;
    std::atomic<uint64_t> m_calls{0};
    std::atomic<uint64_t> m_failures{0};
    Driver m_driver = nullptr;
};

// call-drive <function> <count>, call-report <function>: logs `autotest: call-report <function> calls=N`.
// Counts span the process lifetime (a script list resets them per script). Record(false, ...) fails the test directly.
bool ExecuteCallCounterCommand(const std::string& name, const std::vector<std::string>& args);

// Sets the driver of the counter `name` from the layer that owns the fixture (the test interpreter): false for an unknown name.
bool SetCallDriver(const char* name, CallCounter::Driver driver);

// Every counter back to zero: the next script of a script list counts only its own calls (StartNextScript).
void ResetCallCounters();

} // namespace obl::test
