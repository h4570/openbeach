// Runs the registered unit tests. Usage: obl_unit_tests [prefix...] [-prefix...]
// A case runs when it matches any include prefix (or none are given) and no "-" exclude prefix.
// Exit code: 0 = all selected cases passed, 1 = a failure, 77 = every selected case was skipped
// (ctest SKIP_RETURN_CODE, used when the user's game files are missing).

#include "core/char16.h"
#include "game/feedback/feedback_vocabulary.h"
#include "importer/src/writers/script_convert.h"
#include "unit_test.h"

#ifdef _WIN32
#include <windows.h>

#include <crtdbg.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace obl::unit {

namespace {
int g_failures = 0;
const char* g_current = "";

#ifdef _WIN32
// The STL's checks (back() on an empty vector, ...) end in the invalid-parameter handler.
void __cdecl InvalidParameter(const wchar_t* expression, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
    std::printf("ABORT %s: invalid parameter (%ls)\n", g_current, expression ? expression : L"?");
    std::fflush(stdout);
    std::_Exit(3);
}
#endif
} // namespace

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

void ReportFailure(const char* file, int line, const std::string& message)
{
    ++g_failures;
    std::printf("  %s(%d): %s\n", file, line, message.c_str());
}

} // namespace obl::unit

int main(int argc, char** argv)
{
    using namespace obl::unit;
#ifdef _WIN32
    // A failed debug assertion (CRT/STL) goes to stderr and ends the run instead of opening a dialog on the user's desktop.
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(&InvalidParameter);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    // Line-buffered even into a pipe or file, so a crash or a sanitizer report on stderr (tools/unit_sanitize.sh) can be
    // placed between the result lines of the cases around it. The MSVC CRT asserts on a zero size and has no line
    // buffering (_IOLBF is full buffering there), and the sanitizer build is Linux-only, so this is skipped on Windows.
#ifndef _MSC_VER
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
    // The feedback words a pack supplies at run time (the converter's table), for the cases that reach them through the logic headers.
    {
        obl::feedback::Vocabulary vocabulary;
        std::string error;
        if (!obl::feedback::ParseVocabulary(obl::convert::FeedbackVocabularyJson(), vocabulary, error)) {
            std::printf("ABORT feedback vocabulary: %s\n", error.c_str());
            return 1;
        }
        obl::feedback::SetVocabulary(std::move(vocabulary));
    }
    int passed = 0, failed = 0, skipped = 0;
    for (const TestCase& test : Registry()) {
        const std::string_view name = test.name;
        bool hasInclude = false, included = false, excluded = false;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg.starts_with('-')) {
                excluded = excluded || name.starts_with(arg.substr(1));
            } else {
                hasInclude = true;
                included = included || name.starts_with(arg);
            }
        }
        if ((hasInclude && !included) || excluded) continue;
        g_current = test.name;
        int before = g_failures;
        std::string skipReason;
        bool skippedCase = false;
        try {
            test.run();
        } catch (const Abort&) {
        } catch (const Skip& skip) {
            skippedCase = true;
            skipReason = skip.reason;
        } catch (const std::exception& e) {
            ReportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
#ifdef _WIN32
        // The Debug CRT reports a write past a block only when that block is freed, often cases later; checking the heap
        // after every case fails the one that wrote it (a no-op without _DEBUG).
        if (!_CrtCheckMemory()) ReportFailure(__FILE__, __LINE__, "the debug heap is corrupted (write after the end of a block)");
#endif
        if (g_failures != before) {
            ++failed;
            std::printf("FAIL %s\n", test.name);
        } else if (skippedCase) {
            ++skipped;
            std::printf("SKIP %s (%s)\n", test.name, skipReason.c_str());
        } else {
            ++passed;
            std::printf("PASS %s\n", test.name);
        }
    }
    std::printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    if (failed) return 1;
    if (passed == 0 && skipped > 0) return 77;
    return passed ? 0 : 1; // a filter that selects nothing is a mistake, not a pass
}
