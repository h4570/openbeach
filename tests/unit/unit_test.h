#pragma once

// Minimal unit-test harness for logic that runs without the game (ctest, docs/testing/unit-tests.md).
// TEST_CASE registers a function; CHECK records a failure and continues; REQUIRE stops the case;
// SKIP marks a case whose input (e.g. files from the user's installation) is unavailable.

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace obl::unit {

struct TestCase {
    const char* name;
    void (*run)();
};

std::vector<TestCase>& Registry();
void ReportFailure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, void (*run)()) { Registry().push_back({name, run}); }
};

// Thrown by REQUIRE/SKIP to leave the current case.
struct Abort {};
struct Skip {
    std::string reason;
};

// A float-to-int cast as x86 SSE executes it (cvttss2si): out of range or NaN gives INT32_MIN. A plain cast is undefined
// there, and fakes that model a game conversion must give the same value on every compiler.
inline int32_t TruncToInt32(double value)
{
    if (!(value > -2147483649.0 && value < 2147483648.0)) return INT32_MIN;
    return static_cast<int32_t>(value);
}

} // namespace obl::unit

#define OBL_UNIT_CONCAT2(a, b) a##b
#define OBL_UNIT_CONCAT(a, b) OBL_UNIT_CONCAT2(a, b)

#define OBL_TEST_CASE(name)                                                                                                                \
    static void OBL_UNIT_CONCAT(UnitTest_, __LINE__)();                                                                                    \
    static ::obl::unit::Registrar OBL_UNIT_CONCAT(UnitRegistrar_, __LINE__)(name, &OBL_UNIT_CONCAT(UnitTest_, __LINE__));                  \
    static void OBL_UNIT_CONCAT(UnitTest_, __LINE__)()

#define OBL_CHECK(cond)                                                                                                                    \
    do {                                                                                                                                   \
        if (!(cond)) ::obl::unit::ReportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");                                                   \
    } while (0)

#define OBL_CHECK_EQ(a, b)                                                                                                                 \
    do {                                                                                                                                   \
        const auto& oblA_ = (a);                                                                                                           \
        const auto& oblB_ = (b);                                                                                                           \
        if (!(oblA_ == oblB_)) {                                                                                                           \
            std::ostringstream oblMsg_;                                                                                                    \
            oblMsg_ << "CHECK_EQ(" #a ", " #b "): " << oblA_ << " != " << oblB_;                                                           \
            ::obl::unit::ReportFailure(__FILE__, __LINE__, oblMsg_.str());                                                                 \
        }                                                                                                                                  \
    } while (0)

#define OBL_REQUIRE(cond)                                                                                                                  \
    do {                                                                                                                                   \
        if (!(cond)) {                                                                                                                     \
            ::obl::unit::ReportFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");                                                          \
            throw ::obl::unit::Abort{};                                                                                                    \
        }                                                                                                                                  \
    } while (0)

#define OBL_SKIP(reason)                                                                                                                   \
    throw ::obl::unit::Skip                                                                                                                \
    {                                                                                                                                      \
        reason                                                                                                                             \
    }
